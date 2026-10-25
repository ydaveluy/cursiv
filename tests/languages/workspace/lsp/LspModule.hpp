#pragma once

#include <memory>
#include <string>

#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "workspace/generated/Workspace/Module.hpp"
#include "workspace/lsp/LspServices.hpp"

namespace ws {

// This storey's overrides, on top of the core module's.
void install_lsp_module(WorkspaceServices& services);

std::unique_ptr<WorkspaceServices> create_lsp_services(
    WorkspaceSharedServices& sharedServices,
    std::string languageId =
        std::string(ws::generated::Workspace::kLanguageId));

// The same one call, one storey up.
struct WorkspaceLanguage {
  std::unique_ptr<WorkspaceSharedServices> shared;
  WorkspaceServices& language;
};

WorkspaceLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace ws
