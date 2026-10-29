// The services of the @CURSIV_NEW_ID@ language.
#pragma once

#include <memory>

#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"

// The shared services belong to the whole workspace, such as its documents.
// The core services belong to this language. To add a service, declare it in
// the core services and create it in `install_core_module`.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedCoreServices : virtual cursiv::SharedCoreServices {};

struct @CURSIV_NEW_CLASS@CoreServices
    : cursiv::CoreServicesFor<@CURSIV_NEW_CLASS@SharedCoreServices> {
  std::unique_ptr<const @CURSIV_NEW_CLASS@Validator> validator;

  explicit @CURSIV_NEW_CLASS@CoreServices(
      @CURSIV_NEW_CLASS@SharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  // Lists the services after the default ones, so that a missing required
  // service is reported by name. Add a line for each service you add.
  void visitServices(
      cursiv::function_ref<void(const cursiv::ServiceSlot&)> yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"validator", true, validator != nullptr});
  }
};

// Installs the services of this language; see CoreModule.cpp.
void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
