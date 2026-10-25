#include "domainmodel/lsp/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "domainmodel/core/CoreServices.hpp"
#include "domainmodel/lsp/DomainModelFormatter.hpp"
#include "domainmodel/lsp/DomainModelRenameProvider.hpp"

namespace dm {

void install_lsp_module(DomainModelServices& services) {
  cursiv::install_default_lsp_services(services);
  services.formatter = std::make_unique<const DomainModelFormatter>(services);
  services.renameProvider =
      std::make_unique<const DomainModelRenameProvider>(services);
}

std::unique_ptr<DomainModelServices> create_lsp_services(
    DomainModelSharedServices& sharedServices, std::string languageId) {
  auto services = std::make_unique<DomainModelServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  cursiv::install_default_core_services(*services);
  install_core_module(*services);
  install_lsp_module(*services);
  return services;
}

DomainModelLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files) {
  auto shared = std::make_unique<DomainModelSharedServices>();
  shared->fileSystemProvider =
      files != nullptr ? std::move(files)
                       : std::make_unique<cursiv::EmptyFileSystemProvider>();
  cursiv::install_default_shared_lsp_services(*shared);
  auto services = create_lsp_services(*shared);
  DomainModelServices& language = *services;
  shared->serviceRegistry->registerServices(std::move(services));
  return {std::move(shared), language};
}

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  sharedServices.serviceRegistry->registerServices(create_lsp_services(
      dynamic_cast<DomainModelSharedServices&>(sharedServices)));
  return true;
}

}  // namespace dm
