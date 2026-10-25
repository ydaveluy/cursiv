// What this language owns beyond what every language owns, and the module
// that fills its container.
#pragma once

#include <memory>

#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/core/validation/@CURSIV_NEW_CLASS@Validator.hpp"

// Two containers per language: the shared one holds what is workspace-wide
// (the document store, the index), the other holds one language's
// services. Add a service of your own here, filled in `install_core_module`.

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedCoreServices : virtual cursiv::SharedCoreServices {};

struct @CURSIV_NEW_CLASS@CoreServices
    : cursiv::CoreServicesFor<@CURSIV_NEW_CLASS@SharedCoreServices> {
  std::unique_ptr<const @CURSIV_NEW_CLASS@Validator> validator;

  explicit @CURSIV_NEW_CLASS@CoreServices(
      @CURSIV_NEW_CLASS@SharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  // One row per service, for whoever asks what a container holds: the
  // language's own rows come after the defaults.
  void visitServices(
      cursiv::function_ref<void(const cursiv::ServiceSlot&)> yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"validator", true, validator != nullptr});
  }
};

void install_core_module(@CURSIV_NEW_CLASS@CoreServices& services);

}  // namespace @CURSIV_NEW_ID@
