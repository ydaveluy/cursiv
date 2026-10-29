#include "cursiv/lsp/navigation/DefaultRenameProvider.hpp"

#include <lsp/types.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/core/workspace/TextDocument.hpp"
#include "cursiv/lsp/navigation/NavigationProviderSupport.hpp"
#include "cursiv/lsp/navigation/ReferenceLocation.hpp"
#include "cursiv/lsp/text/Position.hpp"
#include "lsp/uri.h"

namespace cursiv {

namespace {

// What a name is written as: the member its record writes, the class of the
// node it writes it into, and whether a repair produced any part of it.
struct NameRole {
  std::string_view feature;
  const AstTypeTag* owner = nullptr;
  bool repaired = false;
};

// The role of the innermost visible record spanning exactly `[begin, end)`
// and writing a member. Such a record lies, in postorder, between the first
// leaf reaching `begin` and the first record running past `end`, children
// before parents, so the first one found is the innermost. Absent when no
// such record exists.
[[nodiscard]] std::optional<NameRole> name_role_at(const Cst& cst,
                                                   TextOffset begin,
                                                   TextOffset end) {
  const Grammar* const grammar = cst.grammar();
  if (grammar == nullptr) return std::nullopt;
  const NodeCount rank = begin == 0 ? 0 : cst.leaves_before(begin - 1);
  if (rank >= cst.leaf_count()) return std::nullopt;
  for (NodeId id = cst.leaf(rank).id; id < cst.size() && cst[id].end <= end;
       ++id) {
    const CstNode& record = cst[id];
    if (record.begin != begin || record.end != end || record.hidden != 0)
      continue;
    const std::string_view feature = grammar->recordFeature(record);
    if (feature.empty()) continue;
    const AstNode* const owner = owner_of_leaf(cst, id);
    if (owner == nullptr) continue;
    // A qualified name may hold a segment the repair inserted.
    bool repaired = false;
    for (NodeId at = cst.subtree_begin(id); at <= id && !repaired; ++at)
      repaired = cst[at].recovered != 0;
    return NameRole{feature, owner->type, repaired};
  }
  return std::nullopt;
}

}  // namespace

std::optional<lsp::Range> DefaultRenameProvider::prepareRename(
    const Document& document, lsp::Position position,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  // The span is the leaf range of either arm of `findDeclaration` -- a
  // cross-reference or a declaration's own name -- so one call answers both.
  // An unresolved reference is still renamable: `rename` fixes the typo alone.
  const Declaration found = _core.references->findDeclaration(
      document, document.textDocument().offsetAt(from_lsp(position)), token);
  if (!found && found.span.end <= found.span.begin) return std::nullopt;
  return to_lsp(
      document.textDocument().rangeAt(found.span.begin, found.span.end));
}

bool DefaultRenameProvider::isNameOfLanguage(
    const Document& home, std::span<const TextRange> spans,
    std::string_view newName, const CancellationToken& token) const {
  // A zero-width record may span an empty substitution; reject empty names
  // explicitly, independently of what the parser recovers.
  if (newName.empty()) return false;
  if (home.language == nullptr || !home.hasAst()) return false;
  const std::string_view text = home.textDocument().getText();

  // Every substitution at once, as the edits will land: two sites may read
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
  const ParseResult reparsed = home.language->parser->parse(candidate, token);

  const ParseResult& before = home.parseResult;
  if (before.accepted && !reparsed.accepted) return false;
  // Under recovery a parse accepts anything and pays for it in diagnostics, so
  // the count is the real verdict and `accepted` alone would pass a keyword.
  if (reparsed.diagnostics.size() > before.diagnostics.size()) return false;

  // The count is not enough either: a repaired document can read a number or
  // a whole expression where the name stood, at no extra cost. Each
  // replacement must keep its own record, not be consumed with surrounding
  // text (for example by a comment it opened), and that record must write
  // what the old name wrote, with no part of it repaired. The old name may
  // have needed a repair: renaming it is how the author fixes it.
  const auto length = static_cast<TextOffset>(newName.size());
  TextOffset at = 0;
  copied = 0;
  for (const TextRange& span : spans) {
    throw_if_cancelled(token);
    at += span.begin - copied;
    copied = span.end;
    const std::optional<NameRole> was =
        name_role_at(before.cst(), span.begin, span.end);
    if (const std::optional<NameRole> now =
            name_role_at(reparsed.cst(), at, at + length);
        !was || !now || now->repaired || now->feature != was->feature ||
        now->owner != was->owner)
      return false;
    at += length;
  }
  return true;
}

std::optional<lsp::WorkspaceEdit> DefaultRenameProvider::rename(
    const Document& document, lsp::Position position, std::string_view newName,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  const References& references = *_core.references;
  const Documents& store = *_core.shared.documents;

  const Declaration found = references.findDeclaration(
      document, document.textDocument().offsetAt(from_lsp(position)), token);
  if (!found) {
    // The unresolved reference, renamed alone: no declaration to walk from, so
    // the one occurrence is the name as written here. The language still
    // judges the new name, in this document.
    if (found.span.end <= found.span.begin) return std::nullopt;
    if (!isNameOfLanguage(document, {&found.span, 1}, newName, token))
      return std::nullopt;
    lsp::WorkspaceEdit edit;
    lsp::Map<lsp::DocumentUri, lsp::Array<lsp::TextEdit>> changes;
    lsp::TextEdit rewrite;
    rewrite.range = to_lsp(
        document.textDocument().rangeAt(found.span.begin, found.span.end));
    rewrite.newText = std::string(newName);
    changes[lsp::DocumentUri::parse(document.uri)].push_back(
        std::move(rewrite));
    edit.changes = std::move(changes);
    return edit;
  }
  // A declaration that writes no name has none to rename: its site would be
  // its whole record.
  const AstNode* target = found.targets.front();
  const Document* home = target->getDocument();
  if (home == nullptr) return std::nullopt;
  const CoreServices* declaring = home->language;
  const NameProvider* names =
      declaring == nullptr ? nullptr : declaring->nameProvider.get();
  if (const std::optional<CstNodeView> declared =
          names == nullptr ? std::nullopt : names->getNameNode(*target, home);
      !declared || !*declared)
    return std::nullopt;

  const std::vector<ReferenceDescription> rows =
      collect_references_per_declaration(
          references, found.targets,
          [](const AstNode&) {
            return FindReferencesOptions{true, kNoDocument};
          },
          token);

  // Every document the edits touch is read back with all its substitutions
  // made, by its own language: what lexes as a name in the declaring grammar
  // may be a keyword in another, and a reference may read differently from
  // the declaration. A refusal is the whole rename's. Rows come grouped by
  // document, in document order.
  std::vector<TextRange> spans;
  for (auto row = rows.begin(); row != rows.end();) {
    throw_if_cancelled(token);
    const DocumentId id = row->sourceDocumentId;
    spans.clear();
    for (; row != rows.end() && row->sourceDocumentId == id; ++row)
      spans.push_back(TextRange{row->sourceBegin, row->sourceEnd});
    const std::shared_ptr<Document> source = store.getDocument(id);
    if (source == nullptr || source->language == nullptr) continue;
    if (!isNameOfLanguage(*source, spans, newName, token)) return std::nullopt;
  }

  lsp::WorkspaceEdit edit;
  lsp::Map<lsp::DocumentUri, lsp::Array<lsp::TextEdit>> changes;
  // The declaration and every reference, which is what `includeDeclaration`
  // buys: a rename that rewrote only the references would leave the workspace
  // pointing at a name nothing declares.
  for (const ReferenceDescription& reference : rows) {
    throw_if_cancelled(token);
    std::optional<lsp::Location> location =
        reference_location(store, reference);
    if (!location.has_value()) continue;
    lsp::TextEdit rewrite;
    rewrite.range = location->range;
    rewrite.newText = std::string(newName);
    changes[std::move(location->uri)].push_back(std::move(rewrite));
  }
  if (changes.empty()) return std::nullopt;
  edit.changes = std::move(changes);
  return edit;
}

}  // namespace cursiv
