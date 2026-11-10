// Which icon a declaration shows, as only the language knows. Asked for a
// node, and for another document's index row.
#pragma once

#include <lsp/types.h>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"

namespace cursiv {

class NodeKindProvider {
 public:
  virtual ~NodeKindProvider() noexcept = default;

  [[nodiscard]] virtual lsp::SymbolKind getSymbolKind(
      const AstNode& node) const = 0;
  [[nodiscard]] virtual lsp::SymbolKind getSymbolKind(
      const AstNodeDescription& description) const = 0;

  [[nodiscard]] virtual lsp::CompletionItemKind getCompletionItemKind(
      const AstNode& node) const = 0;
  [[nodiscard]] virtual lsp::CompletionItemKind getCompletionItemKind(
      const AstNodeDescription& description) const = 0;
};

}  // namespace cursiv
