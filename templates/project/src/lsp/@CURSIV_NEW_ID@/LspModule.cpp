#include "@CURSIV_NEW_ID@/LspModule.hpp"

#include "cursiv/services/LspLanguage.hpp"
#include "@CURSIV_NEW_ID@/CoreServices.hpp"

namespace @CURSIV_NEW_ID@ {

void install_lsp_module(@CURSIV_NEW_CLASS@Services& /*services*/) {}

// Creates the services of this language for the language server: the
// defaults, then the core module, then the LSP module.
void register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  cursiv::register_lsp_language<@CURSIV_NEW_CLASS@Services,
                                @CURSIV_NEW_CLASS@SharedServices,
                                install_core_module, install_lsp_module>(
      sharedServices);
}

}  // namespace @CURSIV_NEW_ID@
