// Go to definition: a position in a document, and the declarations the
// name under it links to. A default is installed by
// `install_lsp_language<C>`, one row in `LspServices::visitServices`.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class DefinitionProvider {
 public:
  virtual ~DefinitionProvider() noexcept = default;

  /**
   * Called under the workspace read lock. `nullopt` is "nothing here",
   * distinct from the empty set the protocol also allows. A
   * `LocationLink` carries the origin span plus a split target: the
   * declaration to preview and the name inside it to select.
   */
  [[nodiscard]] virtual std::optional<std::vector<lsp::LocationLink>>
  getDefinition(const Document& document, lsp::Position position,
                const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
