// Find references: the inverse of go-to-definition, taking the same
// position in -- every place in the workspace that names the symbol
// under the caret.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class ReferencesProvider {
 public:
  virtual ~ReferencesProvider() noexcept = default;

  /**
   * Called under the workspace read lock. Returns `Location`s, not
   * `LocationLink`s: every entry is itself an origin in its own file, so
   * a flat range is all a client needs. `includeDeclaration` is honoured
   * as asked; no match or an unresolved caret both answer empty.
   */
  [[nodiscard]] virtual std::vector<lsp::Location> findReferences(
      const Document& document, lsp::Position position, bool includeDeclaration,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
