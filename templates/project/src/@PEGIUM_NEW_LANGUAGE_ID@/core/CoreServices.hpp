#pragma once

#include <memory>

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/validation/@PEGIUM_NEW_CLASS@Validator.hpp>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

/// Workspace-wide shared services.
struct @PEGIUM_NEW_CLASS@SharedCoreServices : virtual pegium::SharedCoreServices {
  // define here custom shared services
};

/// Core (headless) @PEGIUM_NEW_CLASS@ language services, built from the language's
/// shared-core type. Add your language services as members.
struct @PEGIUM_NEW_CLASS@CoreServices
    : pegium::CoreServicesFor<@PEGIUM_NEW_CLASS@SharedCoreServices> {
  std::unique_ptr<validation::@PEGIUM_NEW_CLASS@Validator> validator;

  explicit @PEGIUM_NEW_CLASS@CoreServices(
      const @PEGIUM_NEW_CLASS@SharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}
};

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
