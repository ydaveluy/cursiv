#include "domainmodel/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/semantic/GrammarSemanticTokenProvider.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "domainmodel/CoreServices.hpp"
#include "domainmodel/DomainModelFormatter.hpp"
#include "domainmodel/DomainModelRenameProvider.hpp"

namespace dm {

void install_lsp_module(DomainModelServices& services) {
  services.semanticTokenProvider =
      std::make_unique<const cursiv::GrammarSemanticTokenProvider>(services);
  services.formatter = std::make_unique<const DomainModelFormatter>(services);
  services.renameProvider =
      std::make_unique<const DomainModelRenameProvider>(services);
}

DomainModelLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files) {
  return cursiv::create_lsp_language<DomainModelServices,
                                     DomainModelSharedServices,
                                     install_core_module, install_lsp_module>(
      std::move(files));
}

void register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  cursiv::register_lsp_language<DomainModelServices, DomainModelSharedServices,
                                install_core_module, install_lsp_module>(
      sharedServices);
}

}  // namespace dm
