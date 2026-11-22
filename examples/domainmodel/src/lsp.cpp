// The domainmodel language server: one language, the standard transport.

#include "cursiv/runtime/LanguageServerMain.hpp"
#include "domainmodel/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::runLanguageServerMain<dm::DomainModelSharedServices>(
      argc, argv, "domainmodel-lsp", &dm::register_lsp_services);
}
