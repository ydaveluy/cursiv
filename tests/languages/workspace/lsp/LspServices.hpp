#pragma once

#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "workspace/core/CoreServices.hpp"

// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same
// object the LSP providers read.

namespace ws {

struct WorkspaceSharedServices : WorkspaceSharedCoreServices,
                                   cursiv::SharedLspServices {};

struct WorkspaceServices final
    : cursiv::LspServicesFor<WorkspaceCoreServices, WorkspaceSharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace ws
