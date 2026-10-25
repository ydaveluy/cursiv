// Draws inferred information between the words -- a type, a parameter name.
// No default: only the language knows what is worth showing that the author
// did not write.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class InlayHintProvider {
 public:
  virtual ~InlayHintProvider() noexcept = default;

  /**
   * Computes the inlay hints in `range`. Bounded because a document long
   * enough to need hints is long enough that computing them all would waste
   * work.
   */
  [[nodiscard]] virtual std::vector<lsp::InlayHint> getInlayHints(
      const Document& document, lsp::Range range,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
