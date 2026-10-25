// Expand selection: `textDocument/selectionRange` asks, for each
// position, the chain of ever-larger spans the caret sits inside --
// exactly the CST's own chain of containing records, up to the root.
#pragma once

#include <lsp/types.h>

#include <span>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class SelectionRangeProvider {
 public:
  virtual ~SelectionRangeProvider() noexcept = default;

  /**
   * Called under the workspace read lock. A position the tree does not cover
   * still gets its own entry, including when the document has no tree,
   * since the reply is read positionally and dropping one would remap
   * every cursor after it.
   */
  [[nodiscard]] virtual std::vector<lsp::SelectionRange> getSelectionRanges(
      const Document& document, std::span<const lsp::Position> positions,
      const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
