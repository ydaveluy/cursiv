// One of the three go-to questions past definition. There is no default:
// what a type definition is belongs to the language, not to the
// framework, so there is nothing generic to install.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class TypeDefinitionProvider {
 public:
  virtual ~TypeDefinitionProvider() noexcept = default;

  /**
   * Called under the workspace read lock. `nullopt` is "nothing here",
   * which the protocol distinguishes from an empty set.
   */
  [[nodiscard]] virtual std::optional<std::vector<lsp::LocationLink>>
  getTypeDefinition(const Document& document, lsp::Position position,
                    const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
