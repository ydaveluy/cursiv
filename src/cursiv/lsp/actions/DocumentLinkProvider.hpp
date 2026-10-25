#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

// The clickable paths a document writes -- an include, an import, a URL in
// a comment. No default: only the grammar knows which text is a path, and
// only the language knows what it resolves against.

namespace cursiv {

class DocumentLinkProvider {
 public:
  virtual ~DocumentLinkProvider() noexcept = default;

  // Precondition: called under the workspace read lock.
  [[nodiscard]] virtual std::vector<lsp::DocumentLink> getDocumentLinks(
      const Document& document, const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
