#pragma once

#include <memory>

#include "arithmetics/core/validation/ArithmeticsValidator.hpp"
#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"

// What this language owns beyond what every language owns, and the module that
// fills its container.

namespace arith {

struct ArithmeticsSharedCoreServices : virtual cursiv::SharedCoreServices {};

struct ArithmeticsCoreServices
    : cursiv::CoreServicesFor<ArithmeticsSharedCoreServices> {
  std::unique_ptr<const ArithmeticsValidator> validator;

  explicit ArithmeticsCoreServices(
      ArithmeticsSharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"validator", true, validator != nullptr});
  }
};

void install_core_module(ArithmeticsCoreServices& services);

}  // namespace arith
