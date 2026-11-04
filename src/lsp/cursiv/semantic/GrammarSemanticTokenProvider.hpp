// Computes semantic tokens for `textDocument/semanticTokens/full` and
// `/range` from the grammar and the name provider. A server announces semantic
// tokens only for a language that installs a provider, so a language asks for
// this one in its `install_lsp_module`; an editor then paints its colours over
// its own syntax rules. Not templated by the grammar: everything read here is
// already erased except the name provider's typed lookup.
#pragma once

#include <lsp/types.h>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/semantic/SemanticTokenProvider.hpp"

namespace cursiv {

class GrammarSemanticTokenProvider : public SemanticTokenProvider {
 public:
  explicit GrammarSemanticTokenProvider(const CoreServices& core) noexcept
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
