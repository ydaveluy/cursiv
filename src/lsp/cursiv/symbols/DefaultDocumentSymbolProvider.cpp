#include "cursiv/symbols/DefaultDocumentSymbolProvider.hpp"

#include <lsp/types.h>

#include <cassert>
#include <cstddef>
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

lsp::DocumentSymbol DefaultDocumentSymbolProvider::createSymbol(
    const Document& document, const AstNode& node, TextRange span,
    CstNodeView nameNode, std::string_view computedName) const {
  lsp::DocumentSymbol symbol;
  symbol.name =
      std::string(computedName.empty() ? nameNode.text() : computedName);
  symbol.kind = services.lspShared.nodeKindProvider->getSymbolKind(node);
  symbol.range = toLsp(document.textDocument().rangeAt(span.begin, span.end));
  symbol.selectionRange =
      toLsp(document.textDocument().rangeAt(nameNode.begin(), nameNode.end()));
  return symbol;
}

std::vector<lsp::DocumentSymbol> DefaultDocumentSymbolProvider::getSymbols(
    const Document& document, const CancellationToken& token) const {
  throwIfCancelled(token);
  if (!document.hasAst()) return {};

  const Cst& cst = document.parseResult.cst();
  const detail::NamedNodes named =
      detail::collect_named_nodes(document, *services.nameProvider, token);

  std::vector<lsp::DocumentSymbol> symbols;
  symbols.reserve(named.nodes.size());
  for (std::size_t i = 0; i < named.nodes.size(); ++i) {
    throwIfCancelled(token);
    const AstNode& node = *named.nodes[i];
    const CstNode& record = cst[node.cstNodeId];
    symbols.push_back(createSymbol(
        document, node, {record.begin, record.end}, named.nameNodes[i],
        services.nameProvider->getName(node).value_or(std::string_view{})));
  }

  // A symbol goes under its nearest named ancestor. Unnamed nodes dissolve, and
  // so do nodes with an empty name, as one a repair inserted: the protocol has
  // none. Owners come later in postorder, so the pass runs from the end.
  constexpr std::size_t kNone = detail::NamedNodes::kNoOwner;
  // Capped: `children` is written out and torn down by recursion. A symbol
  // that would nest deeper sits beside its owner.
  constexpr std::size_t kMaxDepth = 64;
  std::vector<std::size_t> keptAt(symbols.size(), kNone);
  std::vector<std::size_t> placed(symbols.size(), kNone);
  std::vector<std::size_t> depth(symbols.size(), 0);
  for (std::size_t i = symbols.size(); i-- > 0;) {
    const std::size_t owner = named.owners[i];
    const std::size_t up = owner == kNone ? kNone : keptAt[owner];
    if (symbols[i].name.empty()) {
      keptAt[i] = up;
      continue;
    }
    keptAt[i] = i;
    placed[i] = up != kNone && depth[up] + 1 == kMaxDepth ? placed[up] : up;
    depth[i] = placed[i] == kNone ? 0 : depth[placed[i]] + 1;
  }

  std::vector<lsp::DocumentSymbol> roots;
  for (std::size_t i = 0; i < symbols.size(); ++i) {
    if (keptAt[i] != i) continue;
    const std::size_t owner = placed[i];
    if (owner == kNone) {
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
