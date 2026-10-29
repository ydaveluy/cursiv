#pragma once

#include <memory>

#include "workspace/lsp/LspServices.hpp"

namespace ws {

// This storey's overrides, on top of the core module's.
void install_lsp_module(WorkspaceServices& services);

std::unique_ptr<WorkspaceServices> create_lsp_services(
    WorkspaceSharedServices& sharedServices);

// The same one call, one storey up.
struct WorkspaceLanguage {
  std::unique_ptr<WorkspaceSharedServices> shared;
  WorkspaceServices& language;
};

WorkspaceLanguage create_lsp_services();

}  // namespace ws
