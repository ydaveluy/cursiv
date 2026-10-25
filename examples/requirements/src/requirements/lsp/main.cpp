// The requirements language server: two languages, the standard transport.

#include "cursiv/lsp/runtime/LanguageServerMain.hpp"
#include "requirements/lsp/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<req::RequirementsSharedServices>(
      argc, argv, "requirements-lsp", &req::register_lsp_services);
}
