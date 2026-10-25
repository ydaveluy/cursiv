// Not templated by the grammar: once a node's name node is in hand, the
// outline is the same code for every language. A node with a name node
// becomes a symbol; one without dissolves into its children, found by
// climbing to the nearest named ancestor since this AST has no child list.
#pragma once

#include <lsp/types.h>

#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/CstNodeView.hpp"
#include "cursiv/core/text/TextRange.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/symbols/DocumentSymbolProvider.hpp"

namespace cursiv {

// Not `final`: a language derives it to change what a symbol is -- its
// kind, its detail, its name -- in `createSymbol` below.
class DefaultDocumentSymbolProvider : public DocumentSymbolProvider {
 public:
  explicit DefaultDocumentSymbolProvider(const LspServices& core) noexcept
      : _core(core) {}

  [[nodiscard]] std::vector<lsp::DocumentSymbol> getSymbols(
      const Document& document,
      const CancellationToken& token = {}) const override;

 protected:
  // One symbol, before it is placed in the tree. An empty `computedName` is
  // a declared name that matched nothing; the name node's text stands in.
  [[nodiscard]] virtual lsp::DocumentSymbol createSymbol(
      const Document& document, const AstNode& node, TextRange span,
      CstNodeView nameNode, std::string_view computedName) const;

  [[nodiscard]] const LspServices& core() const noexcept { return _core; }

 private:
  const LspServices& _core;
};

}  // namespace cursiv
