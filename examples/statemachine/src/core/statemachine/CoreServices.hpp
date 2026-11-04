#pragma once

#include <memory>

#include "cursiv/services/CoreServicesFor.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "statemachine/validation/StateMachineValidator.hpp"

// What this language owns beyond what every language owns, and the module
// that fills its container.

namespace sm {

struct StateMachineSharedCoreServices : virtual cursiv::SharedCoreServices {};

struct StateMachineCoreServices
    : cursiv::CoreServicesFor<StateMachineSharedCoreServices> {
  std::unique_ptr<const StateMachineValidator> validator;

  using CoreServicesFor::CoreServicesFor;

  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"validator", true, validator != nullptr});
  }
};

void install_core_module(StateMachineCoreServices& services);

}  // namespace sm
