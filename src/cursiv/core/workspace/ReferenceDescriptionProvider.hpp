#pragma once

#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"

// Every reference the document writes, resolved or not: the inverse index's
// only source.

namespace cursiv {

struct Document;

class ReferenceDescriptionProvider {
 public:
  virtual ~ReferenceDescriptionProvider() noexcept = default;

  // The whole list, in document order. Cancellable like every per-document
  // pass. Not a visit: the only caller keeps the rows anyway, so a callback
  // bought a `function_ref` hop per reference and saved no allocation.
  [[nodiscard]] virtual std::vector<ReferenceDescription> createDescriptions(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) const = 0;
};

}  // namespace cursiv
