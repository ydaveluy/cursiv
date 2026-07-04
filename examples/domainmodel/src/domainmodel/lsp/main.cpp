#include <domainmodel/lsp/LspModule.hpp>

#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

int main(int argc, char **argv) {
  return pegium::runLanguageServerMain<domainmodel::DomainModelSharedServices>(
      argc, argv, "domainmodel-lsp",
      domainmodel::registerDomainModelLspServices);
}
