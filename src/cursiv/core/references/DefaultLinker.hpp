#pragma once

#include "cursiv/core/references/Linker.hpp"
#include "cursiv/core/references/LinkingError.hpp"
#include "cursiv/core/references/LinkingResult.hpp"
#include "cursiv/core/services/DefaultCoreService.hpp"

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

  ~DefaultLinker() noexcept override = default;

 protected:
  // What a name that resolved to nothing is told to be. The one place the
  // sentence is written, so a language rewords it once for both doors.
  // `description` is what the name did reach, when the caller has one; the
  // default doors pass none.
  [[nodiscard]] virtual LinkingError createLinkingError(
      const ReferenceInfo& reference,
      const AstNodeDescription* description = nullptr) const;
};

}  // namespace cursiv
