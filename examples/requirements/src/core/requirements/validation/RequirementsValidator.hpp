#pragma once

#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_set>

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/utils/WorkspaceCache.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "requirements/ast.hpp"

// The two checks of a `.req` document. Whether a requirement is covered
// depends on the other language's documents, so it is computed once per build
// for the whole workspace.

namespace req {

class RequirementsValidator final : public cursiv::DefaultCoreService,
                                    private cursiv::DocumentUpdateListener {
 public:
  explicit RequirementsValidator(const cursiv::CoreServices& services);
  ~RequirementsValidator() noexcept override;

  RequirementsValidator(const RequirementsValidator&) = delete;
  RequirementsValidator& operator=(const RequirementsValidator&) = delete;

  void checkRequirementNameContainsANumber(
      const ast::Requirement& requirement,
      cursiv::ValidationAcceptor& accept) const;
  void checkRequirementIsCoveredByATest(
      const ast::Requirement& requirement,
      cursiv::ValidationAcceptor& accept) const;

 private:
  using Covered = std::unordered_set<const ast::Requirement*>;

  [[nodiscard]] std::shared_ptr<const Covered> computeCoverage() const;

  // Any update can cover or uncover a requirement without relinking its
  // document, so it sends every validated requirement document back to
  // validation.
  void onUpdate(std::span<const std::string_view> changed,
                std::span<const std::string_view> deleted) override;

  // Coverage is one workspace-wide answer. A consumer borrows the owning
  // value under the cache's guard, so hits change no shared reference count
  // and an update cannot release the answer while the check is using it.
  cursiv::WorkspaceCache<std::shared_ptr<const Covered>> _covered;
  mutable std::mutex _coverageMutex;
};

}  // namespace req
