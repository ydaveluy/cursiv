#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

// What this extends, and what extends it. Same three steps as the call
// hierarchy, and for the same reason: the client expands one level per
// request.

namespace cursiv {

class TypeHierarchyProvider {
 public:
  virtual ~TypeHierarchyProvider() noexcept = default;

  // Precondition: called under the workspace read lock.
  [[nodiscard]] virtual std::vector<lsp::TypeHierarchyItem>
  prepareTypeHierarchy(const Document& document, lsp::Position position,
                       const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::TypeHierarchyItem> supertypes(
      const lsp::TypeHierarchyItem& item,
      const CancellationToken& = {}) const = 0;

  [[nodiscard]] virtual std::vector<lsp::TypeHierarchyItem> subtypes(
      const lsp::TypeHierarchyItem& item,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
