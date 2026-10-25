#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

// The commands an editor draws above a line -- run this test, show these
// callers. No default: which construct is worth a lens is the language's
// own. No resolve step either: a lens is answered complete, `resolveProvider`
// false at the handshake, so nothing ever asks to resolve one.

namespace cursiv {

class CodeLensProvider {
 public:
  virtual ~CodeLensProvider() noexcept = default;

  // Precondition: called under the workspace read lock.
  [[nodiscard]] virtual std::vector<lsp::CodeLens> provideCodeLens(
      const Document& document, const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
