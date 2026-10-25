#pragma once

#include <memory>

#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "requirements/core/validation/RequirementsValidator.hpp"
#include "requirements/core/validation/TestsValidator.hpp"

// Two languages over one rule set, so one shared type and one language type:
// a `.tst` reaches the requirements its `.req` neighbour declares, and it does
// it through the workspace they share.

namespace req {

struct RequirementsSharedCoreServices : virtual cursiv::SharedCoreServices {};

struct RequirementsCoreServices
    : cursiv::CoreServicesFor<RequirementsSharedCoreServices> {
  // One container type, two languages: each module fills the slot its own
  // grammar owns and leaves the other empty.
  std::unique_ptr<const RequirementsValidator> requirementsValidator;
  std::unique_ptr<const TestsValidator> testsValidator;

  explicit RequirementsCoreServices(
      RequirementsSharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  // Optional both: each of the two languages fills its own and leaves the
  // other empty.
  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"requirementsValidator", false, requirementsValidator != nullptr});
    yield({"testsValidator", false, testsValidator != nullptr});
  }
};

}  // namespace req
