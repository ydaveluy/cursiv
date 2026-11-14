#include "cursiv/symbols/DefaultDocumentSymbolProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/references/NameProvider.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/symbols/NamedNodes.hpp"
#include "cursiv/symbols/NodeKindProvider.hpp"
#include "cursiv/text/LspPosition.hpp"

namespace cursiv {

namespace {

// Covers every record the entry emitted, not just `entryRoot`'s node: a
// trailing comment the entry consumed sits past the root. Node ids are
// postorder, so this is a min/max fold, not a first/last read.
[[nodiscard]] TextRange entry_span(const Cst& cst) {
  const NodeId begin = cst.entryBegin();
  const NodeId end = cst.entryEnd();
  if (end <= begin) return TextRange{};
  TextRange span{cst[begin].begin, cst[begin].end};
  for (NodeId i = begin + 1; i < end; ++i) {
    span.begin = std::min(span.begin, cst[i].begin);
    span.end = std::max(span.end, cst[i].end);
  }
  return span;
}

}  // namespace

lsp::DocumentSymbol DefaultDocumentSymbolProvider::createSymbol(
    const Document& document, const AstNode& node, TextRange span,
    CstNodeView nameNode, std::string_view computedName) const {
  lsp::DocumentSymbol symbol;
  symbol.name =
      std::string(computedName.empty() ? nameNode.text() : computedName);
  symbol.kind = services.lspShared.nodeKindProvider->getSymbolKind(node);
  symbol.range = to_lsp(document.textDocument().rangeAt(span.begin, span.end));
  symbol.selectionRange =
      to_lsp(document.textDocument().rangeAt(nameNode.begin(), nameNode.end()));
  return symbol;
}

std::vector<lsp::DocumentSymbol> DefaultDocumentSymbolProvider::getSymbols(
    const Document& document, const CancellationToken& token) const {
  throw_if_cancelled(token);
  if (!document.hasAst()) return {};

  const Cst& cst = document.parseResult.cst();
  const detail::NamedNodes named =
      detail::collect_named_nodes(document, *services.nameProvider, token);
  std::optional<TextRange> entry;

  std::vector<lsp::DocumentSymbol> symbols;
  symbols.reserve(named.nodes.size());
  for (std::size_t i = 0; i < named.nodes.size(); ++i) {
    throw_if_cancelled(token);
    const AstNode& node = *named.nodes[i];
    const NodeId id = node.cstNodeId;
    if (id == cst.entryRoot() && !entry.has_value()) entry = entry_span(cst);
    const TextRange span =
        id == cst.entryRoot() ? *entry : TextRange{cst[id].begin, cst[id].end};
    symbols.push_back(createSymbol(
        document, node, span, named.nameNodes[i],
        services.nameProvider->getName(node).value_or(std::string_view{})));
  }

  // The owner of a symbol is its nearest named ancestor; unnamed nodes
  // dissolve.
  std::vector<lsp::DocumentSymbol> roots;
  for (std::size_t i = 0; i < symbols.size(); ++i) {
    const std::size_t owner = named.owners[i];
    if (owner == detail::NamedNodes::kNoOwner) {
      roots.push_back(std::move(symbols[i]));
      continue;
    }
    assert(owner > i && "postorder: an owner is built after what it owns");
    // Absent, not empty, for a symbol that owns none: the protocol's `children`
    // is optional.
    if (!symbols[owner].children.has_value()) symbols[owner].children.emplace();
    symbols[owner].children->push_back(std::move(symbols[i]));
  }
  return roots;
}

}  // namespace cursiv
