#pragma once

#include <lsp/types.h>

#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/workspace/Symbol.hpp>

namespace pegium {

/// Maps AST nodes and symbol descriptions to LSP symbol/completion kinds.
class NodeKindProvider {
public:
  virtual ~NodeKindProvider() noexcept = default;

  [[nodiscard]] virtual ::lsp::SymbolKindEnum
  getSymbolKind(const pegium::AstNode &node) const = 0;

  [[nodiscard]] virtual ::lsp::SymbolKindEnum
  getSymbolKind(const workspace::AstNodeDescription &description) const = 0;

  [[nodiscard]] virtual ::lsp::CompletionItemKindEnum
  getCompletionItemKind(const pegium::AstNode &node) const = 0;

  [[nodiscard]] virtual ::lsp::CompletionItemKindEnum
  getCompletionItemKind(const workspace::AstNodeDescription &description) const = 0;
};

} // namespace pegium
