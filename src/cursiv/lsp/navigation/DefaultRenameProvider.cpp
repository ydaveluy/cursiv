#include "cursiv/lsp/navigation/DefaultRenameProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

bool DefaultRenameProvider::isNameOfLanguage(const Document& home,
                                             TextRange span,
                                             std::string_view newName,
                                             const CancellationToken& token) const {
  // A zero-width record may span an empty substitution; reject empty names
  // explicitly, independently of what the parser recovers.
  if (newName.empty()) return false;
  if (home.language == nullptr || !home.hasAst()) return false;
  const std::string_view text = home.textDocument().getText();
  if (span.end < span.begin || span.end > text.size()) return false;

  std::string candidate(text);
  candidate.replace(span.begin, span.end - span.begin, newName);
  // The tree slices `candidate`, so both live exactly as long as this frame.
  const ParseResult reparsed = home.language->parser->parse(candidate, token);

  const ParseResult& before = home.parseResult;
  if (before.accepted && !reparsed.accepted) return false;
  // Under recovery a parse accepts anything and pays for it in diagnostics, so
  // the count is the real verdict and `accepted` alone would pass a keyword.
  if (reparsed.diagnostics.size() > before.diagnostics.size()) return false;

  // The replacement must retain its own CST range, instead of being consumed
  // together with surrounding text, for example by a comment it opened.
  const TextOffset begin = span.begin;
  const TextOffset end = begin + static_cast<TextOffset>(newName.size());
  const Cst& cst = reparsed.cst();
  for (NodeId id = 0; id < cst.size(); ++id) {
    throw_if_cancelled(token);
    if (cst[id].begin == begin && cst[id].end == end) return true;
  }
  return false;
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
    if (!isNameOfLanguage(document, found.span, newName, token))
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
  // The name is validated once, against the first target's declaring document:
  // the whole group shares one written name, so one language's verdict on the
  // substitution is every member's.
  const AstNode* target = found.targets.front();
  const Document* home = target->getDocument();
  if (home == nullptr) return std::nullopt;

  // Where the declaration's own name is written, in the declaring document --
  // which is what the substitution has to be made at, and is not `found.span`
  // when the caret was on a reference in another file.
  const CoreServices* declaring = home->language;
  const NameProvider* names =
      declaring == nullptr ? nullptr : declaring->nameProvider.get();
  const CstNodeView declared =
      names == nullptr
          ? CstNodeView{}
          : names->getNameNode(*target, home).value_or(CstNodeView{});
  if (!declared) return std::nullopt;
  if (!isNameOfLanguage(*home, TextRange{declared.begin(), declared.end()},
                        newName, token))
    return std::nullopt;

  const std::vector<ReferenceDescription> rows =
      collect_references_per_declaration(
          references, found.targets,
          [](const AstNode&) {
            return FindReferencesOptions{true, kNoDocument};
          },
          token);

  // One judgement per language the edits touch: the substitution is written
  // into every referencing document, and what lexes as a name in the declaring
  // grammar may be a keyword in another. A refusal is the whole rename's.
  std::vector<const CoreServices*> judged{declaring};
  for (const ReferenceDescription& reference : rows) {
    throw_if_cancelled(token);
    const std::shared_ptr<Document> source =
        store.getDocument(reference.sourceDocumentId);
    if (source == nullptr || source->language == nullptr) continue;
    if (std::ranges::find(judged, source->language) != judged.end()) continue;
    judged.push_back(source->language);
    if (!isNameOfLanguage(*source,
                          TextRange{reference.sourceBegin, reference.sourceEnd},
                          newName, token))
      return std::nullopt;
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
