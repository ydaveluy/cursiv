// Translates the core `References` answer. Names come from the target
// document's language, not the asking one.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/navigation/DefinitionProvider.hpp"

namespace cursiv {

class DefaultDefinitionProvider : public DefinitionProvider {
 public:
  explicit DefaultDefinitionProvider(const CoreServices& core) noexcept
      : _core(core) {}

  [[nodiscard]] std::optional<std::vector<lsp::LocationLink>> getDefinition(
      const Document& document, lsp::Position position,
      const CancellationToken& token = {}) const override;

 private:
  const CoreServices& _core;
};

}  // namespace cursiv
