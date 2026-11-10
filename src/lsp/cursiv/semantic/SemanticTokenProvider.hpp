// `textDocument/semanticTokens`: five integers per token, each relative to
// the one before. `full`, `range`, and `full/delta`, which sends only edits.
#pragma once

#include <lsp/types.h>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class SemanticTokenProvider {
 public:
  virtual ~SemanticTokenProvider() noexcept = default;

  // The legend and the requests answered. The server merges its languages'
  // legends index by index, so two languages must not name different things at
  // one index. `delta` and `range` are announced only if all answer them.
  [[nodiscard]] virtual lsp::SemanticTokensOptions semanticTokensOptions()
      const = 0;

  // Every token of the request's document, under the workspace read lock. An
  // empty `data` means the document never parsed.
  [[nodiscard]] virtual lsp::SemanticTokens semanticHighlight(
      const Document& document, const lsp::SemanticTokensParams& params,
      const CancellationToken& = {}) const = 0;

  // The tokens that touch the request's range, under the same rules.
  [[nodiscard]] virtual lsp::SemanticTokens semanticHighlightRange(
      const Document& document, const lsp::SemanticTokensRangeParams& params,
      const CancellationToken& = {}) const = 0;

  // The edits from the answer `previousResultId` names to the tokens now, or
  // the tokens whole when that answer is not the document's last.
  [[nodiscard]] virtual lsp::OneOf<lsp::SemanticTokens,
                                   lsp::SemanticTokensDelta>
  semanticHighlightDelta(const Document& document,
                         const lsp::SemanticTokensDeltaParams& params,
                         const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
