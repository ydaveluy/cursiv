#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/utils/WorkspaceCache.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "requirements/core/ast.hpp"

// The two checks a `.req` document owes. The second is the reason this
// example exists: whether a requirement is covered is a fact about the other
// language's documents, so it is computed once per build and memoised
// workspace-wide.

namespace req {

class RequirementsValidator final : public cursiv::DefaultCoreService {
 public:
  explicit RequirementsValidator(const cursiv::CoreServices& services);

  void checkRequirementNameContainsANumber(
      const ast::Requirement& requirement,
      cursiv::ValidationAcceptor& accept) const;
  void checkRequirementIsCoveredByATest(
      const ast::Requirement& requirement,
      cursiv::ValidationAcceptor& accept) const;

 private:
  using Covered = std::unordered_set<const ast::Requirement*>;

  [[nodiscard]] std::shared_ptr<const Covered> computeCoverage() const;

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
