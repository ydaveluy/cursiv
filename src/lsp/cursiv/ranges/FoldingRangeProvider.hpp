// What the editor can collapse: `textDocument/foldingRange` answers a flat
// list of ranges, each an AST node's record or a comment record spanning
// three lines or more, the closer left visible.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/Document.hpp"

namespace cursiv {

class FoldingRangeProvider {
 public:
  virtual ~FoldingRangeProvider() noexcept = default;

  // Called under the workspace read lock. Both line and column bounds are sent;
  // a client that folds by line ignores the columns.
  [[nodiscard]] virtual std::vector<lsp::FoldingRange> getFoldingRanges(
      const Document& document, const CancellationToken& = {}) const = 0;
};

using FoldingRangeAcceptor = function_ref<void(lsp::FoldingRange)>;

}  // namespace cursiv
