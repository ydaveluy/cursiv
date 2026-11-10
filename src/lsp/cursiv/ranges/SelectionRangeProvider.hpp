// Expand selection: for each position, the ever-larger spans holding it, the
// CST's containing records up to the root.
#pragma once

#include <lsp/types.h>

#include <span>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class SelectionRangeProvider {
 public:
  virtual ~SelectionRangeProvider() noexcept = default;

  // Called under the workspace read lock. Each position gets an entry, even one
  // the tree does not cover: the reply is read by position.
  [[nodiscard]] virtual std::vector<lsp::SelectionRange> getSelectionRanges(
      const Document& document, std::span<const lsp::Position> positions,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
