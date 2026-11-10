// Find references: every place in the workspace that names the symbol under
// the caret.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class ReferencesProvider {
 public:
  virtual ~ReferencesProvider() noexcept = default;

  // Called under the workspace read lock. `includeDeclaration` is honoured; no
  // match answers empty.
  [[nodiscard]] virtual std::vector<lsp::Location> findReferences(
      const Document& document, lsp::Position position, bool includeDeclaration,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
