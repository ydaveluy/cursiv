// A thin translation: what a reference resolves to is the core
// `References` service's answer already. The one twist is the name
// provider used per target -- the target's own document's language, not
// the asking one, since a cross-language reference otherwise has none.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/navigation/DefinitionProvider.hpp"

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
