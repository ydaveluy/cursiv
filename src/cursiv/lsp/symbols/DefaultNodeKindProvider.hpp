// The kind a language has not chosen: one neutral constant per question. The
// framework cannot guess, and a wrong icon reads worse than a neutral one.
#pragma once

#include <lsp/types.h>

#include "cursiv/lsp/symbols/NodeKindProvider.hpp"

namespace cursiv {

class DefaultNodeKindProvider : public NodeKindProvider {
 public:
  [[nodiscard]] lsp::SymbolKind getSymbolKind(const AstNode&) const override;
  [[nodiscard]] lsp::SymbolKind getSymbolKind(
      const AstNodeDescription&) const override;

  [[nodiscard]] lsp::CompletionItemKind getCompletionItemKind(
      const AstNode&) const override;
  [[nodiscard]] lsp::CompletionItemKind getCompletionItemKind(
      const AstNodeDescription&) const override;
};

}  // namespace cursiv
