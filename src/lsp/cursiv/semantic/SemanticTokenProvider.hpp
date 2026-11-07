// Semantic colouring for `textDocument/semanticTokens`: one flat array of
// unsigned integers, five per token, each stated as a delta from the one
// before it. `full`, `range`, and `full/delta`, which sends only the edits
// from the client's last answer.
#pragma once

#include <lsp/types.h>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class SemanticTokenProvider {
 public:
  virtual ~SemanticTokenProvider() noexcept = default;

  /**
   * The legend a token's type index and modifier bits refer to, and the
   * requests answered. The server announces one legend for all its
   * languages: theirs are merged index by index, and two languages naming a
   * different type or modifier at the same index cannot share a server.
   * `delta` and `range` are announced only where every language answers them.
   */
  [[nodiscard]] virtual lsp::SemanticTokensOptions semanticTokensOptions()
      const = 0;

  /**
   * Every semantic token of the request's document. Precondition: called
   * under the workspace read lock. An empty `data` means the document never
   * parsed -- the client reads it as no colouring.
   */
  [[nodiscard]] virtual lsp::SemanticTokens semanticHighlight(
      const Document& document, const lsp::SemanticTokensParams& params,
      const CancellationToken& = {}) const = 0;

  /**
   * The tokens that touch the request's range, encoded and preconditioned
   * the same way.
   */
  [[nodiscard]] virtual lsp::SemanticTokens semanticHighlightRange(
      const Document& document, const lsp::SemanticTokensRangeParams& params,
      const CancellationToken& = {}) const = 0;

  /**
   * The edits that turn the answer `previousResultId` names into the
   * document's tokens now, or those tokens whole when that answer is not the
   * last one given for the document. Preconditioned the same way.
   */
  [[nodiscard]] virtual lsp::OneOf<lsp::SemanticTokens,
                                   lsp::SemanticTokensDelta>
  semanticHighlightDelta(const Document& document,
                         const lsp::SemanticTokensDeltaParams& params,
                         const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
