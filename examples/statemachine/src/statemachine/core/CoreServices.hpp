#pragma once

#include <memory>

#include <statemachine/core/validation/StatemachineValidator.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace statemachine {

/// Workspace-wide shared services.
struct StatemachineSharedCoreServices : virtual pegium::SharedCoreServices {
  // define here custom shared services
};

/// Core (headless) language services, built from the language's shared-core type.
struct StatemachineCoreServices
    : pegium::CoreServicesFor<StatemachineSharedCoreServices> {
  std::unique_ptr<validation::StatemachineValidator> validator;

  explicit StatemachineCoreServices(
      const StatemachineSharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

} // namespace statemachine
