// The services of the language server: the core services of the language plus
// the editor services. To add an editor service, declare it here.
#pragma once

#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedServices : @CURSIV_NEW_CLASS@SharedCoreServices,
                                           cursiv::SharedLspServices {};

struct @CURSIV_NEW_CLASS@Services final : @CURSIV_NEW_CLASS@CoreServices,
                                           cursiv::LspServices {
  explicit @CURSIV_NEW_CLASS@Services(
      @CURSIV_NEW_CLASS@SharedServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        @CURSIV_NEW_CLASS@CoreServices(sharedServices),
        cursiv::LspServices(sharedServices) {}

  // Lists the core services, then the editor services.
  void visitServices(
      cursiv::function_ref<void(const cursiv::ServiceSlot&)> yield) const override {
    @CURSIV_NEW_CLASS@CoreServices::visitServices(yield);
    cursiv::LspServices::visitOwnServices(yield);
  }
};

}  // namespace @CURSIV_NEW_ID@
