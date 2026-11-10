#pragma once

#include <vector>

#include "cursiv/references/Linker.hpp"
#include "cursiv/references/LinkingError.hpp"
#include "cursiv/services/DefaultCoreService.hpp"

namespace cursiv {

class DefaultLinker : public Linker, protected DefaultCoreService {
 public:
  using DefaultCoreService::DefaultCoreService;

  void link(Document& document,
            const CancellationToken& cancelToken = kNoCancel) const override;
  void unlink(Document& document) const override;

  [[nodiscard]] const AstNodeDescription* getCandidate(
      const ReferenceInfo& reference) const override;
  [[nodiscard]] std::vector<const AstNodeDescription*> getCandidates(
      const ReferenceInfo& reference) const override;

  [[nodiscard]] LinkingError createLinkingError(
      const ReferenceInfo& reference, RefState why = RefState::ErrorNotFound,
      const AstNodeDescription* description = nullptr) const override;

  ~DefaultLinker() noexcept override = default;
};

}  // namespace cursiv
