#pragma once

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/CoreServices.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@ {

/// Workspace-wide shared services in an LSP session.
struct @PEGIUM_NEW_CLASS@SharedServices
    : @PEGIUM_NEW_CLASS@SharedCoreServices, pegium::SharedServices {};

/// LSP-enabled @PEGIUM_NEW_CLASS@ language services.
struct @PEGIUM_NEW_CLASS@Services final
    : pegium::ServicesFor<@PEGIUM_NEW_CLASS@CoreServices,
                          @PEGIUM_NEW_CLASS@SharedServices> {
  explicit @PEGIUM_NEW_CLASS@Services(
      const @PEGIUM_NEW_CLASS@SharedServices &sharedServices)
      : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
};

} // namespace @PEGIUM_NEW_LANGUAGE_ID@
