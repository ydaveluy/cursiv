#pragma once

#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "workspace/core/CoreServices.hpp"

// Both bases derive `CoreServices` virtually, so the core module and the LSP
// providers use the same object.

namespace ws {

struct WorkspaceSharedServices : WorkspaceSharedCoreServices,
                                   cursiv::SharedLspServices {};

struct WorkspaceServices final
    : cursiv::LspServicesFor<WorkspaceCoreServices, WorkspaceSharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace ws
