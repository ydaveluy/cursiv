#pragma once

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspServices.hpp"

namespace @CURSIV_NEW_ID@ {

// Installs the editor services: the defaults, then your own, such as a
// formatter or a hover provider. Add yours in LspModule.cpp.
void install_lsp_module(@CURSIV_NEW_CLASS@Services& services);

// Adds this language to the workspace of the language server; `main` passes
// it to the server.
bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace @CURSIV_NEW_ID@
