#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

// Who calls this, and what does it call. Three steps because the client
// walks the tree one level at a time: a root is prepared once, then each
// expansion is its own request.

namespace cursiv {

class CallHierarchyProvider {
 public:
  virtual ~CallHierarchyProvider() noexcept = default;

  // Precondition: called under the workspace read lock.
  [[nodiscard]] virtual std::vector<lsp::CallHierarchyItem>
  prepareCallHierarchy(const Document& document, lsp::Position position,
                       const CancellationToken& = {}) const = 0;

  // `item` must be one this provider returned: it carries a URI and a range,
  // never a node.
  [[nodiscard]] virtual std::vector<lsp::CallHierarchyIncomingCall>
  incomingCalls(const lsp::CallHierarchyItem& item,
                const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::CallHierarchyOutgoingCall>
  outgoingCalls(const lsp::CallHierarchyItem& item,
                const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
