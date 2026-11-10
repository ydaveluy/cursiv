// Go to implementation. No default: what an implementation is belongs to the
// language.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class ImplementationProvider {
 public:
  virtual ~ImplementationProvider() noexcept = default;

  // Called under the workspace read lock. `nullopt` and an empty list both
  // answer an empty list.
  [[nodiscard]] virtual std::optional<std::vector<lsp::LocationLink>>
  getImplementation(const Document& document, lsp::Position position,
                    const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
