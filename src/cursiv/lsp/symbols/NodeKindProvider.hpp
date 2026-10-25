// Which icon a declaration wears: the client draws an outline row or a
// completion entry from a kind, and only the language knows that an
// `Entity` is a class and a `Feature` a field. Two shapes, since a symbol
// is asked for both from a node and from another document's index row.
#pragma once

#include <lsp/types.h>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"

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
