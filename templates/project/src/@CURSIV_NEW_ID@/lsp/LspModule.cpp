#include "@CURSIV_NEW_ID@/lsp/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

void install_lsp_module(@CURSIV_NEW_CLASS@Services& services) {
  cursiv::install_default_lsp_services(services);
}

// Creates the services of this language for the language server: the
// defaults, then the core module, then the LSP module.
bool register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  auto& shared =
      dynamic_cast<@CURSIV_NEW_CLASS@SharedServices&>(sharedServices);
  auto services = std::make_unique<@CURSIV_NEW_CLASS@Services>(shared);
  cursiv::install_default_core_services(*services);
  install_core_module(*services);
  install_lsp_module(*services);
  shared.serviceRegistry->registerServices(std::move(services));
  return true;
}

}  // namespace @CURSIV_NEW_ID@
