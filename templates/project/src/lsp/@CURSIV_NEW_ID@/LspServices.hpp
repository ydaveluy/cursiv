// The services of the language server: the core services of the language plus
// the editor services. To add an editor service, declare it here.
#pragma once

#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "@CURSIV_NEW_ID@/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

struct @CURSIV_NEW_CLASS@SharedServices : @CURSIV_NEW_CLASS@SharedCoreServices,
                                           cursiv::SharedLspServices {};

struct @CURSIV_NEW_CLASS@Services final
    : cursiv::LspServicesFor<@CURSIV_NEW_CLASS@CoreServices, @CURSIV_NEW_CLASS@SharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace @CURSIV_NEW_ID@
