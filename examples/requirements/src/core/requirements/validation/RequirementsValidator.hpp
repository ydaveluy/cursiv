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

// The two checks a `.req` document owes. The second is the reason this
// example exists: whether a requirement is covered is a fact about the other
// language's documents, so it is computed once per build and memoised
// workspace-wide.

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

  // A `.req` names no `.tst`, so an edited test relinks no requirement
  // document, yet it can cover or uncover any requirement: an update that
  // touches a document of another language sends every validated
  // requirement document back to validation.
  void onUpdate(std::span<const std::string_view> changed,
                std::span<const std::string_view> deleted) override;

  // One key: the answer is the whole workspace's, and every `.req` of a build
  // asks it. `_owner` holds it and `_covered` hands it out bare: a hit on the
  // owner would count a reference on one shared line, per requirement, from
  // every worker. Both empty on the announcements that open a build, so no
  // check holds the pointer across one.
  cursiv::WorkspaceCache<int, std::shared_ptr<const Covered>> _owner;
  cursiv::WorkspaceCache<int, const Covered*> _covered;
  mutable std::mutex _coverageMutex;
};

}  // namespace req
