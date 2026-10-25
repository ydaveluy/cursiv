// The descent, and nothing else: the record containing the offset, then
// the child containing it, until none does -- read outside-in, published
// inside-out. Downwards because a `CstNode` carries no parent link, only
// its own subtree extent.
#pragma once

#include <lsp/types.h>

#include <span>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/ranges/SelectionRangeProvider.hpp"

namespace cursiv {

class DefaultSelectionRangeProvider : public SelectionRangeProvider {
 public:
  [[nodiscard]] std::vector<lsp::SelectionRange> getSelectionRanges(
      const Document& document, std::span<const lsp::Position> positions,
      const CancellationToken& token = {}) const override;
};

}  // namespace cursiv
