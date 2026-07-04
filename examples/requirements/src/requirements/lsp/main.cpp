#include <requirements/lsp/LspModule.hpp>

#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

int main(int argc, char **argv) {
  return pegium::runLanguageServerMain<requirements::RequirementsSharedServices>(
      argc, argv, "requirements-lsp",
      requirements::registerRequirementsLspServices);
}
