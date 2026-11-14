#pragma once

#include <lsp/types.h>

#include <optional>

#include "cursiv/hover/HoverProvider.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

// A declaration's prose comes from the language that declares it.

namespace cursiv {

class DefaultHoverProvider : public HoverProvider,
                             protected DefaultCoreService {
 public:
  explicit DefaultHoverProvider(const CoreServices& core) noexcept
      : DefaultCoreService(core) {}

  [[nodiscard]] std::optional<lsp::Hover> getHoverContent(
      const Document& document, lsp::Position position,
      const CancellationToken& token = {}) const override;
};

}  // namespace cursiv
