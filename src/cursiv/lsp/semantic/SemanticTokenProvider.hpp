// Semantic colouring for `textDocument/semanticTokens`: one flat array of
// unsigned integers, five per token, each stated as a delta from the one
// before it. `full` and `range`; no `delta`, which would need a cache.
#pragma once

#include <lsp/types.h>

#include <cstdint>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

// The legend is the server's, not a language's: one array announced at the
// handshake indexes every document of every language, so an enumerator's
// value is its index into `semantic_tokens_legend().tokenTypes`.
enum class SemanticTokenType : std::uint32_t {
  Keyword = 0,   // a guarded literal: a word the grammar reserved
  Type = 1,      // a declaration that owns other declarations
  Variable = 2,  // a reference: a name standing for a declaration elsewhere
  Property = 3,  // a declaration that owns none: a member of its owner
  Number = 4,    // a leaf whose text opens like a number
  String = 5,    // a leaf whose text opens with a quote
  Comment = 6,   // a record of the grammar's `hidden` list
  Operator = 7,  // an unguarded literal: punctuation
};

/**
 * The one legend, announced at `initialize` and indexed by every token.
 */
[[nodiscard]] const lsp::SemanticTokensLegend& semantic_tokens_legend();

class SemanticTokenProvider {
 public:
  virtual ~SemanticTokenProvider() noexcept = default;

  /**
   * Computes every semantic token of `document`. Precondition: called under
   * the workspace read lock. An empty `data` means the document never
   * parsed -- the client reads it as no colouring.
   */
  [[nodiscard]] virtual lsp::SemanticTokens semanticHighlight(
      const Document& document, const CancellationToken& = {}) const = 0;

  /**
   * The tokens that touch `range`, encoded and preconditioned the same way.
   */
  [[nodiscard]] virtual lsp::SemanticTokens semanticHighlightRange(
      const Document& document, lsp::Range range,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
