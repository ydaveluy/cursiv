// What the editor can collapse: `textDocument/foldingRange` answers a flat
// list of ranges, each an AST node's record or a comment record spanning
// three lines or more, the closer left visible.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class FoldingRangeProvider {
 public:
  virtual ~FoldingRangeProvider() noexcept = default;

  /**
   * Called under the workspace read lock. Both units are published: the
   * columns are the record's own bounds, read by a client that folds
   * mid-line and ignored by one that folds by line.
   */
  [[nodiscard]] virtual std::vector<lsp::FoldingRange> getFoldingRanges(
      const Document& document, const CancellationToken& = {}) const = 0;
};

using FoldingRangeAcceptor = function_ref<void(lsp::FoldingRange)>;

}  // namespace cursiv
