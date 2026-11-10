#pragma once

#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"

// Every resolved reference the document writes, one row per target: the
// inverse index's only source.

namespace cursiv {

struct Document;

class ReferenceDescriptionProvider {
 public:
  virtual ~ReferenceDescriptionProvider() noexcept = default;

  // The whole list, in document order. Cancellable like every per-document
  // pass.
  [[nodiscard]] virtual std::vector<ReferenceDescription> createDescriptions(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) const = 0;
};

}  // namespace cursiv
