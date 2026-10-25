#pragma once

#include <memory>

#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "statemachine/core/validation/StateMachineValidator.hpp"

// What this language owns beyond what every language owns, and the module
// that fills its container.

namespace sm {

struct StateMachineSharedCoreServices : virtual cursiv::SharedCoreServices {};

struct StateMachineCoreServices
    : cursiv::CoreServicesFor<StateMachineSharedCoreServices> {
  std::unique_ptr<const StateMachineValidator> validator;

  explicit StateMachineCoreServices(
      StateMachineSharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"validator", true, validator != nullptr});
  }
};

void install_core_module(StateMachineCoreServices& services);

}  // namespace sm
