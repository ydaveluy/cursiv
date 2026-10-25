#pragma once

#include <lsp/types.h>

#include <optional>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

// Hover: a position in a document, and the prose the language attaches to
// what stands there, measured entirely inside that document -- no lookup
// needed. Nothing is a valid answer, never an error: a position on nothing,
// on undocumented text, or in a document that never parsed.

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
