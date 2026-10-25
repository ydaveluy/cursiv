#pragma once

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/ReferenceDescription.hpp"
#include "cursiv/core/workspace/ReferenceDescriptionProvider.hpp"

// Grammar-free, for the reason the export provider is: what a reference is,
// where its name was written and what it resolved to are read off the
// document's own `DocumentReferences`, which its language's generated module
// made. It resolves nothing -- the two walks it calls are reads, so indexing a
// document never triggers the linking it is supposed to describe.

namespace cursiv {

class DefaultReferenceDescriptionProvider : public ReferenceDescriptionProvider,
                                            protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  [[nodiscard]] std::vector<ReferenceDescription> createDescriptions(
      const Document& document,
      const CancellationToken& cancelToken = kNoCancel) const override;
};

}  // namespace cursiv
