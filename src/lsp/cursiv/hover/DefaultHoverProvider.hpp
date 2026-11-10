#pragma once

#include <lsp/types.h>

#include <optional>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/hover/HoverProvider.hpp"

// A declaration's prose comes from the language that declares it.

namespace cursiv {

class DefaultHoverProvider : public HoverProvider {
 public:
  explicit DefaultHoverProvider(const CoreServices& core) noexcept
      : _core(core) {}

  [[nodiscard]] std::optional<lsp::Hover> getHoverContent(
      const Document& document, lsp::Position position,
      const CancellationToken& token = {}) const override;

 private:
  const CoreServices& _core;
};

}  // namespace cursiv
