#include "cursiv/lsp/hover/DefaultHoverProvider.hpp"

#include <lsp/types.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/documentation/DocumentationProvider.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/References.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

namespace {

// The existing leaf index locates the token without scanning its siblings.
// A gap between leaves reaches no record, as does text outside the entry.
[[nodiscard]] NodeId leaf_at(const Cst& cst, TextOffset offset) {
  if (!cst.has_entry()) return kNoNode;
  const CstNode& entry = cst[cst.entry_root()];
  if (offset < entry.begin || offset >= entry.end) return kNoNode;
  const NodeCount rank = cst.leaves_before(offset);
  if (rank == cst.leaf_count()) return kNoNode;
  const CstLeaf leaf = cst.leaf(rank);
  return leaf.begin <= offset ? leaf.id : kNoNode;
}

}  // namespace

std::optional<lsp::Hover> DefaultHoverProvider::getHoverContent(
    const Document& document, lsp::Position position,
    const CancellationToken& token) const {
  throw_if_cancelled(token);
  if (!document.hasAst()) return std::nullopt;

  const TextOffset offset =
      document.textDocument().offsetAt(from_lsp(position));
  // Last, not first: this matches the leaf's spelling and not the terminal that
  // produced it, and a keyword is a legal name.
  const auto keywordHover = [&]() -> std::optional<lsp::Hover> {
    const Cst& cst = document.parseResult.cst();
    // A CST span is half-open, so a click at a keyword's very end lands one
    // past it -- the same retry the definition provider makes.
    NodeId leaf = leaf_at(cst, offset);
    if (leaf == kNoNode && offset > 0) leaf = leaf_at(cst, offset - 1);
    if (leaf == kNoNode) return std::nullopt;
    const std::string_view doc =
        _core.parser->getGrammar().spellingDoc(cst.text(leaf));
    if (doc.empty()) return std::nullopt;
    const CstNode& node = cst[leaf];
    lsp::Hover hover;
    lsp::MarkupContent content;
    content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::Markdown);
    content.value = std::string(doc);
    hover.contents = std::move(content);
    hover.range = to_lsp(document.textDocument().rangeAt(node.begin, node.end));
    return hover;
  };

  // What a name under the caret means is the declaration it denotes -- both
  // arms of `References::findDeclaration`, the same call every navigation
  // service makes here -- and its prose comes from the `DocumentationProvider`
  // of the language that declares it.
  const Declaration found =
      _core.references->findDeclaration(document, offset, token);
  if (!found) return keywordHover();

  // Every target contributes its prose, the pieces joined with a space.
  std::string joined;
  for (const AstNode* target : found.targets) {
    throw_if_cancelled(token);
    const Document* home = target->getDocument();
    if (home == nullptr) continue;
    const CoreServices* language = home->language;
    const DocumentationProvider* const docs =
        language == nullptr ? nullptr : language->documentationProvider.get();
    if (docs == nullptr) continue;
    if (std::optional<std::string> prose =
            docs->getDocumentation(*target, home)) {
      if (!joined.empty()) joined += ' ';
      joined += *prose;
    }
  }
  if (joined.empty()) return keywordHover();

  lsp::Hover hover;
  lsp::MarkupContent content;
  content.kind = lsp::MarkupKindEnum(lsp::MarkupKind::Markdown);
  content.value = std::move(joined);
  hover.contents = std::move(content);
  // The range is the name as written here, the word the popup is about.
  hover.range =
      to_lsp(document.textDocument().rangeAt(found.span.begin, found.span.end));
  return hover;
}

}  // namespace cursiv
