// Translates the core `References` answer. Names come from the target
// document's language, not the asking one.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/navigation/DefinitionProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class DefaultDefinitionProvider : public DefinitionProvider,
                                  protected DefaultCoreService {
 public:
  explicit DefaultDefinitionProvider(const CoreServices& core) noexcept
      : DefaultCoreService(core) {}

  [[nodiscard]] std::optional<std::vector<lsp::LocationLink>> getDefinition(
      const Document& document, lsp::Position position,
      const CancellationToken& token = {}) const override;
};

}  // namespace cursiv
