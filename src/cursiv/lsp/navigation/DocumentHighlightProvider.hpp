// Document highlight: the same symbol, painted where it appears in this
// document and nowhere else -- the restriction is the protocol's own, and
// the whole difference from find-references.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class DocumentHighlightProvider {
 public:
  virtual ~DocumentHighlightProvider() noexcept = default;

  /**
   * Called under the workspace read lock. A caret on a reference whose
   * declaration lives elsewhere still answers, with the references here
   * alone; empty is never an error.
   */
  [[nodiscard]] virtual std::vector<lsp::DocumentHighlight>
  getDocumentHighlight(const Document& document, lsp::Position position,
                       const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
