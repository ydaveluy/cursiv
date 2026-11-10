#pragma once

#include <lsp/types.h>

#include <optional>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

// The prose the language attaches to what stands at a position. Nothing is an
// answer, never an error.

namespace cursiv {

class HoverProvider {
 public:
  virtual ~HoverProvider() noexcept = default;

  // Precondition: called under the workspace read lock.
  [[nodiscard]] virtual std::optional<lsp::Hover> getHoverContent(
      const Document& document, lsp::Position position,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
