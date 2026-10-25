#pragma once

#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "workspace/core/CoreServices.hpp"

// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same
// object the LSP providers read.

namespace ws {

struct WorkspaceSharedServices : WorkspaceSharedCoreServices,
                                   cursiv::SharedLspServices {};

struct WorkspaceServices final : WorkspaceCoreServices,
                                   cursiv::LspServices {
  explicit WorkspaceServices(WorkspaceSharedServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        WorkspaceCoreServices(sharedServices),
        cursiv::LspServices(sharedServices) {}

  // One emission per storey through the diamond: the language rows through
  // the core side, the LSP rows alone.
  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    WorkspaceCoreServices::visitServices(yield);
    cursiv::LspServices::visitOwnServices(yield);
  }
};

}  // namespace ws
