#include "cursiv/navigation/DefaultRenameProvider.hpp"

#include <lsp/types.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/references/NameProvider.hpp"
#include "cursiv/references/References.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/CstUtils.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/text/LspPosition.hpp"
#include "lsp/uri.h"

namespace cursiv {

namespace {

// What a name is written as: the member its record writes, the class of the
// node it writes it into, and whether a repair produced any part of it.
struct NameRole {
  std::string_view property;
  const AstTypeTag* owner = nullptr;
  bool repaired = false;
};

// The role of the innermost visible record that spans exactly `[begin, end)`
// and writes a member; none if there is no such record.
[[nodiscard]] std::optional<NameRole> name_role_at(const Cst& cst,
                                                   TextOffset begin,
                                                   TextOffset end) {
  const Grammar* const grammar = cst.grammar();
  if (grammar == nullptr) return std::nullopt;
  const NodeId first = begin == 0 ? 0 : cst.leafAfter(begin - 1);
  if (first == cst.size()) return std::nullopt;
  for (NodeId id = first; id < cst.size() && cst[id].end <= end; ++id) {
    const CstNode& record = cst[id];
    if (record.begin != begin || record.end != end || record.isHidden())
      continue;
    const std::string_view property = grammar->recordProperty(record);
    if (property.empty()) continue;
    const AstNode* const owner = ownerOfLeaf(cst, id);
    if (owner == nullptr) continue;
    // A qualified name may hold a segment the repair inserted.
    bool repaired = false;
    for (NodeId at = cst.subtreeBegin(id); at <= id && !repaired; ++at)
      repaired = cst[at].isRecovered();
    return NameRole{property, owner->type, repaired};
  }
  return std::nullopt;
}

}  // namespace

std::optional<lsp::Range> DefaultRenameProvider::prepareRename(
    const Document& document, lsp::Position position,
    const CancellationToken& token) const {
  throwIfCancelled(token);
  // The span is the leaf range of either arm of `findDeclarations` -- a
  // cross-reference or a declaration's own name -- so one call answers both.
  // An unresolved reference is still renamable: `rename` fixes the typo alone.
  const Declaration found = services.references->findDeclarations(
      document, document.textDocument().offsetAt(fromLsp(position)), token);
  if (!found && found.span.end <= found.span.begin) return std::nullopt;
  return toLsp(
      document.textDocument().rangeAt(found.span.begin, found.span.end));
}

bool DefaultRenameProvider::isNameOfLanguage(
    const Document& declaringDocument, std::span<const TextRange> spans,
    std::string_view newName, const CancellationToken& token) const {
  // A zero-width record may span an empty substitution; reject empty names
  // explicitly, independently of what the parser recovers.
  if (newName.empty()) return false;
  if (declaringDocument.language == nullptr || !declaringDocument.hasAst())
    return false;
  const std::string_view text = declaringDocument.textDocument().getText();

  // Every substitution at once, as the edits apply: two sites may read
  // differently side by side than alone.
  std::string candidate;
  TextOffset copied = 0;
  for (const TextRange& span : spans) {
    if (span.begin < copied || span.end < span.begin || span.end > text.size())
      return false;
    candidate.append(text.substr(copied, span.begin - copied));
    candidate.append(newName);
    copied = span.end;
  }
  candidate.append(text.substr(copied));
  // The tree slices `candidate`, so both live exactly as long as this frame.
  const ParseResult reparsed =
      declaringDocument.language->parser->parse(candidate, token);

  const ParseResult& before = declaringDocument.parseResult;
  if (before.accepted && !reparsed.accepted) return false;
  // Under recovery a parse accepts anything and pays for it in diagnostics, so
  // the count is the real verdict and `accepted` alone would pass a keyword.
  if (reparsed.diagnostics.size() > before.diagnostics.size()) return false;

  // Each replacement must keep a record of its own, with no repaired part, that
  // writes what the old name wrote. The old name may itself have been repaired.
  const auto length = static_cast<TextOffset>(newName.size());
  TextOffset at = 0;
  copied = 0;
  for (const TextRange& span : spans) {
    throwIfCancelled(token);
    at += span.begin - copied;
    copied = span.end;
    const std::optional<NameRole> was =
        name_role_at(before.cst(), span.begin, span.end);
    if (const std::optional<NameRole> now =
            name_role_at(reparsed.cst(), at, at + length);
        !was || !now || now->repaired || now->property != was->property ||
        now->owner != was->owner)
      return false;
    at += length;
  }
  return true;
}

std::optional<lsp::WorkspaceEdit> DefaultRenameProvider::rename(
    const Document& document, lsp::Position position, std::string_view newName,
    const CancellationToken& token) const {
  throwIfCancelled(token);
  const References& references = *services.references;
  const Documents& store = *services.shared().documents;

  const Declaration found = references.findDeclarations(
      document, document.textDocument().offsetAt(fromLsp(position)), token);
  lsp::WorkspaceEdit edit;
  auto& changes = edit.changes.emplace();
  const auto rewrite = [&](const Document& source,
                           std::span<const TextRange> spans) {
    if (!isNameOfLanguage(source, spans, newName, token)) return false;
    auto& edits = changes[lsp::Uri::parse(source.uri)];
    edits.reserve(spans.size());
    for (const TextRange span : spans) {
      throwIfCancelled(token);
      edits.push_back(
          {.range = toLsp(source.textDocument().rangeAt(span.begin, span.end)),
           .newText = std::string(newName)});
    }
    return true;
  };
  if (!found) {
    if (found.span.end <= found.span.begin ||
        !rewrite(document, {&found.span, 1}))
      return std::nullopt;
    return edit;
  }
  // A declaration that writes no name has none to rename: its site would be
  // its whole record.
  const AstNode* target = found.targets.front();
  const Document* declaringDocument = target->getDocument();
  if (declaringDocument == nullptr) return std::nullopt;
  const auto* language = declaringDocument->language;
  if (!language || !language->nameProvider ||
      !language->nameProvider->getNameNode(*target, declaringDocument))
    return std::nullopt;

  const std::vector<ReferenceDescription> rows =
      references.findReferences(found.targets, {true, kNoDocument}, token);

  // Each touched document is read back with all its substitutions, by its own
  // language; one refusal refuses the whole rename. Rows come grouped by
  // document, in document order.
  std::vector<TextRange> spans;
  for (auto row = rows.begin(); row != rows.end();) {
    throwIfCancelled(token);
    const DocumentId id = row->sourceDocumentId;
    spans.clear();
    for (; row != rows.end() && row->sourceDocumentId == id; ++row)
      spans.push_back(TextRange{row->sourceBegin, row->sourceEnd});
    const std::shared_ptr<Document> source = store.getDocument(id);
    if (source && !rewrite(*source, spans)) return std::nullopt;
  }
  if (changes.empty()) return std::nullopt;
  return edit;
}

}  // namespace cursiv
