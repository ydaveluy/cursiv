#pragma once

#include <memory>

#include "cursiv/services/CoreServicesFor.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "requirements/validation/RequirementsValidator.hpp"
#include "requirements/validation/TestsValidator.hpp"

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

  using CoreServicesFor::CoreServicesFor;

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
