#pragma once

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspServices.hpp"

namespace @CURSIV_NEW_ID@ {

// The LSP storey of this language: the framework's defaults, then whatever the
// language overrides -- a formatter, a hover provider, a semantic token
// provider. Add it here.
void install_lsp_module(@CURSIV_NEW_CLASS@Services& services);

// Files this language into a workspace the server owns. What `main` hands to
// the runner, which has already installed the workspace-wide services.
bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace @CURSIV_NEW_ID@
