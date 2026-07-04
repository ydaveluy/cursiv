#pragma once

#include <memory>

#include <arithmetics/core/validation/ArithmeticsValidator.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace arithmetics {

/// Workspace-wide shared services.
struct ArithmeticsSharedCoreServices : virtual pegium::SharedCoreServices {
  // define here custom shared services
};

/// Core (headless) language services, built from the language's shared-core type.
struct ArithmeticsCoreServices
    : pegium::CoreServicesFor<ArithmeticsSharedCoreServices> {
  std::unique_ptr<validation::ArithmeticsValidator> validator;

  explicit ArithmeticsCoreServices(
      const ArithmeticsSharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

} // namespace arithmetics
