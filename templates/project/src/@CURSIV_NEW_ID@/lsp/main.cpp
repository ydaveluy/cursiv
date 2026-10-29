// The @CURSIV_NEW_ID@ language server. The editor starts it and talks to it
// over standard input and output.

#include "cursiv/lsp/runtime/LanguageServerMain.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedServices>(
      argc, argv, "@CURSIV_NEW_ID@-lsp", &@CURSIV_NEW_ID@::register_lsp_services);
}
