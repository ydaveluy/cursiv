// A document's outline: the protocol's own `lsp::DocumentSymbol`, nested
// through its `children`, so publication is a move and not a translation.
// It takes the document and nothing else, so a symbol's offsets and the
// index that converts them are always the same document's.
#pragma once

#include <lsp/types.h>

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"

namespace cursiv {

class DocumentSymbolProvider {
 public:
  virtual ~DocumentSymbolProvider() noexcept = default;

  /**
   * Computes the outline of `document`. Precondition: called under the
   * workspace read lock.
   */
  [[nodiscard]] virtual std::vector<lsp::DocumentSymbol> getSymbols(
      const Document& document, const CancellationToken& = {}) const = 0;
};

}  // namespace cursiv
