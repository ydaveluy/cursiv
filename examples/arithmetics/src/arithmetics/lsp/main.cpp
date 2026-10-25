// The arithmetics language server: one language, the standard transport.

#include "arithmetics/lsp/LspModule.hpp"
#include "cursiv/lsp/runtime/LanguageServerMain.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<arith::ArithmeticsSharedServices>(
      argc, argv, "arithmetics-lsp", &arith::register_lsp_services);
}
