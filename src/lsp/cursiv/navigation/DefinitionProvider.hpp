// Go to definition: the declarations the name at a position links to.
// `install_default_lsp_services` installs the default.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class DefinitionProvider {
 public:
  virtual ~DefinitionProvider() noexcept = default;

  // Called under the workspace read lock. `nullopt` and an empty list both
  // answer an empty list. A link gives the declaration to preview and the name
  // to select.
  [[nodiscard]] virtual std::optional<std::vector<lsp::LocationLink>>
  getDefinition(const Document& document, lsp::Position position,
                const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
