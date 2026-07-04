#pragma once

#include <statemachine/core/CoreServices.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace statemachine {

/// Workspace-wide shared services in an LSP session.
struct StatemachineSharedServices : StatemachineSharedCoreServices,
                                    pegium::SharedServices {};

/// LSP-enabled statemachine language services.
struct StatemachineServices final
    : pegium::ServicesFor<StatemachineCoreServices, StatemachineSharedServices> {
  explicit StatemachineServices(const StatemachineSharedServices &sharedServices)
      : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
};

} // namespace statemachine
