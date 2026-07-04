#pragma once

#include <memory>

#include <requirements/core/validation/RequirementsValidator.hpp>
#include <requirements/core/validation/TestsValidator.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace requirements {

/// Workspace-wide shared services, shared by both languages of this example.
struct RequirementsSharedCoreServices : virtual pegium::SharedCoreServices {
  // define here custom shared services
};

/// Core (headless) requirements-language services, built from the language's
/// shared-core type.
struct RequirementsCoreServices
    : pegium::CoreServicesFor<RequirementsSharedCoreServices> {
  std::unique_ptr<validation::RequirementsValidator> validator;

  explicit RequirementsCoreServices(
      const RequirementsSharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

/// Core (headless) tests-language services, built from the language's
/// shared-core type.
struct TestsCoreServices : pegium::CoreServicesFor<RequirementsSharedCoreServices> {
  std::unique_ptr<validation::TestsValidator> validator;

  explicit TestsCoreServices(const RequirementsSharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

} // namespace requirements
