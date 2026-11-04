#pragma once

#include "cursiv/services/DefaultCoreService.hpp"
#include "cursiv/validation/ValidationAcceptor.hpp"
#include "requirements/ast.hpp"

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
