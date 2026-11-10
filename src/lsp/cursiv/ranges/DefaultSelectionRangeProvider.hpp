// Descends from the record containing the offset to the innermost child that
// contains it, and answers inside-out.
#pragma once

#include <lsp/types.h>

#include <span>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/ranges/SelectionRangeProvider.hpp"

namespace cursiv {

class DefaultSelectionRangeProvider : public SelectionRangeProvider {
 public:
  [[nodiscard]] std::vector<lsp::SelectionRange> getSelectionRanges(
      const Document& document, std::span<const lsp::Position> positions,
      const CancellationToken& token = {}) const override;
};

}  // namespace cursiv
