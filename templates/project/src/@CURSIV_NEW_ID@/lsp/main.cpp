// The @CURSIV_NEW_ID@ language server: one language, the standard transport
// (stdio -- the editor spawns this binary and speaks the protocol to it).

#include "cursiv/lsp/runtime/LanguageServerMain.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedServices>(
      argc, argv, "@CURSIV_NEW_ID@-lsp", &@CURSIV_NEW_ID@::register_lsp_services);
}
