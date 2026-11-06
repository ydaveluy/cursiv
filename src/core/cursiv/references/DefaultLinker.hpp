#pragma once

#include "cursiv/references/Linker.hpp"
#include "cursiv/references/LinkingError.hpp"
#include "cursiv/references/LinkingResult.hpp"
#include "cursiv/services/DefaultCoreService.hpp"

// Not templated: the two loops that need the grammar -- forcing and clearing --
// live on the document's own `DocumentReferences`, which its language's
// generated module made. What is left is a scope lookup, which is erased.

namespace cursiv {

// Not `final`: a language derives this linker to reword how a failed lookup
// reads, which is what `createLinkingError` below is.
class DefaultLinker : public Linker, protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  void link(Document& document,
            const CancellationToken& cancelToken = kNoCancel) const override;
  void unlink(Document& document) const override;

  [[nodiscard]] AstNodeDescriptionOrError getCandidate(
      const ReferenceInfo& reference) const override;
  [[nodiscard]] AstNodeDescriptionsOrError getCandidates(
      const ReferenceInfo& reference) const override;

  // The default lookups pass no description.
  [[nodiscard]] LinkingError createLinkingError(
      const ReferenceInfo& reference, RefState why = RefState::ErrorNotFound,
      const AstNodeDescription* description = nullptr) const override;

  ~DefaultLinker() noexcept override = default;
};

}  // namespace cursiv
