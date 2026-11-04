// The arithmetics language server: one language, the standard transport.

#include "arithmetics/LspModule.hpp"
#include "cursiv/runtime/LanguageServerMain.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<arith::ArithmeticsSharedServices>(
      argc, argv, "arithmetics-lsp", &arith::register_lsp_services);
}
