#pragma once

#include "cursiv/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/LspServices.hpp"

namespace @CURSIV_NEW_ID@ {

// Installs your own editor services, such as a formatter or a hover
// provider, over the defaults. Add yours in LspModule.cpp.
void install_lsp_module(@CURSIV_NEW_CLASS@Services& services);

// Adds this language to the workspace of the language server; `main` passes
// it to the server.
void register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace @CURSIV_NEW_ID@
