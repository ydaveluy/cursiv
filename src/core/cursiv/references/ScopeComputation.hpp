#pragma once

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/AstNodeDescription.hpp"
#include "cursiv/workspace/LocalSymbols.hpp"

// The two symbol tables one document derives from its own tree, before
// linking. Neither call reads another document.

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
