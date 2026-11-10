// Draws inferred information between the words -- a type, a parameter name.
// No default: only the language knows what is worth showing that the author
// did not write.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class InlayHintProvider {
 public:
  virtual ~InlayHintProvider() noexcept = default;

  [[nodiscard]] virtual std::vector<lsp::InlayHint> getInlayHints(
      const Document& document, lsp::Range range,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
