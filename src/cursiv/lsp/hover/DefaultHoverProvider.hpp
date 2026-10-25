#pragma once

#include <lsp/types.h>

#include <optional>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/hover/HoverProvider.hpp"

// Not templated by the grammar: the walk to a position's leaf is CST
// topology, resolved through the documentation-provider interface. Its own
// container serves keyword prose; a referenced declaration's prose instead
// comes from whichever language declares it, not necessarily this one.

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
