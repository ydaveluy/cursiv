#pragma once

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/ReferenceDescription.hpp"
#include "cursiv/workspace/ReferenceDescriptionProvider.hpp"

// Grammar-free: references are read off the document's `DocumentReferences`.
// It only reads, so indexing a document never triggers its linking.

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
