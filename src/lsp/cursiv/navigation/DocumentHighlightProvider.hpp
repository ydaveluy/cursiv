// Document highlight: the occurrences of a symbol in this document only.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class DocumentHighlightProvider {
 public:
  virtual ~DocumentHighlightProvider() noexcept = default;

  // Called under the workspace read lock. A reference declared elsewhere still
  // answers, with the references in this document.
  [[nodiscard]] virtual std::vector<lsp::DocumentHighlight>
  getDocumentHighlight(const Document& document, lsp::Position position,
                       const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
