#pragma once

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/AstNodeDescription.hpp"
#include "cursiv/core/workspace/LocalSymbols.hpp"

// The two symbol tables one document derives from its own tree: neither call
// reads another document, so this phase itself needs no barrier -- though the
// builder still bars it, since linking that follows must find every
// document's local symbols already written. Neither table resolves a
// reference; scope computation runs before linking exists.

namespace cursiv {

struct Document;

class ScopeComputation {
 public:
  virtual ~ScopeComputation() noexcept = default;

  // What this document makes visible to the others -- the index's input.
  [[nodiscard]] virtual std::vector<AstNodeDescription> collectExportedSymbols(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // What is visible from inside this document's own tree.
  [[nodiscard]] virtual LocalSymbols collectLocalSymbols(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) const = 0;
};

}  // namespace cursiv
