#include "arithmetics/lsp/LspModule.hpp"

#include <memory>
#include <utility>

#include "arithmetics/core/CoreServices.hpp"
#include "arithmetics/lsp/ArithmeticsCodeActionProvider.hpp"
#include "arithmetics/lsp/ArithmeticsFormatter.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"

namespace arith {

void install_lsp_module(ArithmeticsServices& services) {
  cursiv::install_default_lsp_services(services);
  services.formatter = std::make_unique<const ArithmeticsFormatter>(services);
  services.codeActionProvider =
      std::make_unique<const ArithmeticsCodeActionProvider>();
}

std::unique_ptr<ArithmeticsServices> create_lsp_services(
    ArithmeticsSharedServices& sharedServices, std::string languageId) {
  auto services = std::make_unique<ArithmeticsServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  cursiv::install_default_core_services(*services);
  install_core_module(*services);
  install_lsp_module(*services);
  return services;
}

ArithmeticsLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files) {
  auto shared = std::make_unique<ArithmeticsSharedServices>();
  shared->fileSystemProvider =
      files != nullptr ? std::move(files)
                       : std::make_unique<cursiv::EmptyFileSystemProvider>();
  cursiv::install_default_shared_lsp_services(*shared);
  auto services = create_lsp_services(*shared);
  ArithmeticsServices& language = *services;
  shared->serviceRegistry->registerServices(std::move(services));
  return {std::move(shared), language};
}

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  sharedServices.serviceRegistry->registerServices(create_lsp_services(
      dynamic_cast<ArithmeticsSharedServices&>(sharedServices)));
  return true;
}

}  // namespace arith
