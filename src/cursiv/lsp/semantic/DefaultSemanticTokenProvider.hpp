// Computes semantic tokens for `textDocument/semanticTokens/full` and
// `/range`. Not templated by the grammar: everything read here is already
// erased except the name provider's typed lookup.
#pragma once

#include <lsp/types.h>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/semantic/SemanticTokenProvider.hpp"

namespace cursiv {

class DefaultSemanticTokenProvider : public SemanticTokenProvider {
 public:
  explicit DefaultSemanticTokenProvider(const CoreServices& core) noexcept
      : _core(core) {}

  [[nodiscard]] lsp::SemanticTokens semanticHighlight(
      const Document& document,
      const CancellationToken& token = {}) const override;
  [[nodiscard]] lsp::SemanticTokens semanticHighlightRange(
      const Document& document, lsp::Range range,
      const CancellationToken& token = {}) const override;

 private:
  // The tokens touching `[begin, end)`, in source order.
  [[nodiscard]] lsp::SemanticTokens highlight(
      const Document& document, TextOffset begin, TextOffset end,
      const CancellationToken& token) const;

  const CoreServices& _core;
};

}  // namespace cursiv
