#pragma once

#include "cursiv/core/services/DefaultCoreService.hpp"
#include "cursiv/core/validation/ValidationAcceptor.hpp"
#include "requirements/core/ast.hpp"

// The two checks on a `.tst`. The second reads across the reference edge into
// the `.req` documents the workspace already linked.

namespace req {

class TestsValidator final : public cursiv::DefaultCoreService {
 public:
  using cursiv::DefaultCoreService::DefaultCoreService;

  void checkTestNameContainsANumber(const ast::Test& test,
                                    cursiv::ValidationAcceptor& accept) const;
  void checkTestReferencesOnlyEnvironmentsAlsoReferencedInSomeRequirement(
      const ast::Test& test, cursiv::ValidationAcceptor& accept) const;
};

}  // namespace req
