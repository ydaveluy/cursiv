#include "workspace/lsp/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "workspace/core/CoreServices.hpp"
#include "workspace/lsp/WorkspaceFormatter.hpp"
#include "workspace/lsp/WorkspaceRenameProvider.hpp"

namespace ws {

void install_lsp_module(WorkspaceServices& services) {
  cursiv::install_default_lsp_services(services);
  services.formatter = std::make_unique<const WorkspaceFormatter>(services);
  services.renameProvider =
      std::make_unique<const WorkspaceRenameProvider>(services);
}

std::unique_ptr<WorkspaceServices> create_lsp_services(
    WorkspaceSharedServices& sharedServices) {
  auto services = std::make_unique<WorkspaceServices>(sharedServices);
  cursiv::install_default_core_services(*services);
  install_core_module(*services);
  install_lsp_module(*services);
  return services;
}

WorkspaceLanguage create_lsp_services() {
  auto shared = std::make_unique<WorkspaceSharedServices>();
  cursiv::install_default_shared_lsp_services(*shared);
  auto services = create_lsp_services(*shared);
  WorkspaceServices& language = *services;
  shared->serviceRegistry->registerServices(std::move(services));
  return {std::move(shared), language};
}

}  // namespace ws
