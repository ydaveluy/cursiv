// The requirements language server: two languages, the standard transport.

#include "cursiv/runtime/LanguageServerMain.hpp"
#include "requirements/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::runLanguageServerMain<req::RequirementsSharedServices>(
      argc, argv, "requirements-lsp", &req::register_lsp_services);
}
