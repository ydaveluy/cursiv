#include <domainmodel/lsp/LspModule.hpp>

#include <utility>

#include <domainmodel/core/CoreModule.hpp>
#include <domainmodel/lsp/DomainModelFormatter.hpp>
#include <domainmodel/lsp/DomainModelRenameProvider.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>

namespace domainmodel {

void installDomainModelLspModule(DomainModelServices &services) {
  services.lsp.renameProvider =
      std::make_unique<DomainModelRenameProvider>(services);
  services.lsp.formatter = std::make_unique<DomainModelFormatter>(services);
}

std::unique_ptr<DomainModelServices>
createDomainModelLspServices(const DomainModelSharedServices &sharedServices,
                          std::string languageId) {
  auto services = pegium::makeDefaultServices<DomainModelServices>(
      sharedServices, std::move(languageId));
  installDomainModelCoreModule(*services);
  installDomainModelLspModule(*services);
  return services;
}

bool registerDomainModelLspServices(pegium::SharedServices &sharedServices) {
  // The session container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(createDomainModelLspServices(
      dynamic_cast<DomainModelSharedServices &>(sharedServices)));
  return true;
}

} // namespace domainmodel
