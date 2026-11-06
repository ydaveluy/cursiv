// One of the three go-to questions past definition. There is no default:
// what a declaration is belongs to the language, not to the framework, so
// there is nothing generic to install.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class DeclarationProvider {
 public:
  virtual ~DeclarationProvider() noexcept = default;

  /**
   * Called under the workspace read lock. `nullopt` and an empty list both
   * say nothing is here; the client receives an empty list.
   */
  [[nodiscard]] virtual std::optional<std::vector<lsp::LocationLink>>
  getDeclaration(const Document& document, lsp::Position position,
                 const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
