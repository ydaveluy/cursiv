// The outline, the same for every language: a node with a name node is a
// symbol, one without dissolves into its children.
#pragma once

#include <lsp/types.h>

#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/CstNodeView.hpp"
#include "cursiv/text/TextRange.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/services/LspServices.hpp"
#include "cursiv/symbols/DocumentSymbolProvider.hpp"

namespace cursiv {

class DefaultDocumentSymbolProvider : public DocumentSymbolProvider {
 public:
  explicit DefaultDocumentSymbolProvider(const LspServices& core) noexcept
      : services(core) {}

  [[nodiscard]] std::vector<lsp::DocumentSymbol> getSymbols(
      const Document& document,
      const CancellationToken& token = {}) const override;

 protected:
  // One symbol, before it is placed in the tree. An empty `computedName` is
  // a declared name that matched nothing; the name node's text stands in.
  [[nodiscard]] virtual lsp::DocumentSymbol createSymbol(
      const Document& document, const AstNode& node, TextRange span,
      CstNodeView nameNode, std::string_view computedName) const;

  const LspServices& services;
};

}  // namespace cursiv
