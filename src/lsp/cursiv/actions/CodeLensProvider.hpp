#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

// The commands an editor draws above a line. No default and no resolve step:
// a lens is answered complete.

namespace cursiv {

class CodeLensProvider {
 public:
  virtual ~CodeLensProvider() noexcept = default;

  // Precondition: called under the workspace read lock.
  [[nodiscard]] virtual std::vector<lsp::CodeLens> provideCodeLens(
      const Document& document, const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
