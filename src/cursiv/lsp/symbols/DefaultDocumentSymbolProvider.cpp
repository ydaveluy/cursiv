#include "cursiv/lsp/symbols/DefaultDocumentSymbolProvider.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/references/NameProvider.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/symbols/NamedNodes.hpp"
#include "cursiv/lsp/symbols/NodeKindProvider.hpp"
#include "cursiv/lsp/text/Position.hpp"

namespace cursiv {

namespace {

// Covers every record the entry emitted, not just `entry_root`'s node: a
// trailing comment the entry consumed sits past the root. Node ids are
// postorder, so this is a min/max fold, not a first/last read.
[[nodiscard]] TextRange entry_span(const Cst& cst) {
  const NodeId begin = cst.entry_begin();
  const NodeId end = cst.entry_end();
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
  symbol.kind = _core.lspShared.nodeKindProvider->getSymbolKind(node);
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
  const NamedNodes named =
      collect_named_nodes(document, *_core.nameProvider, token);
  std::optional<TextRange> entry;

  std::vector<lsp::DocumentSymbol> symbols;
  symbols.reserve(named.nodes.size());
  for (std::size_t i = 0; i < named.nodes.size(); ++i) {
    throw_if_cancelled(token);
    const AstNode& node = *named.nodes[i];
    const NodeId id = node.cstNodeId;
    if (id == cst.entry_root() && !entry.has_value()) entry = entry_span(cst);
    const TextRange span =
        id == cst.entry_root() ? *entry : TextRange{cst[id].begin, cst[id].end};
    symbols.push_back(createSymbol(
        document, node, span, named.nameNodes[i],
        _core.nameProvider->getName(node).value_or(std::string_view{})));
  }

  // The owner of a symbol is its nearest named ancestor; everything else
  // dissolves, exactly as a descent that recursed through unnamed nodes would.
  std::vector<lsp::DocumentSymbol> roots;
  for (std::size_t i = 0; i < symbols.size(); ++i) {
    const std::size_t owner = named.owners[i];
    if (owner == NamedNodes::kNoOwner) {
      roots.push_back(std::move(symbols[i]));
      continue;
    }
    assert(owner > i && "postorder: an owner is built after what it owns");
    // Absent, not empty, for a symbol that owns none: the protocol's
    // `children` is optional and a leaf owes the client no empty array.
    if (!symbols[owner].children.has_value()) symbols[owner].children.emplace();
    symbols[owner].children->push_back(std::move(symbols[i]));
  }
  return roots;
}

}  // namespace cursiv
