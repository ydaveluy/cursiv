#include <statemachine/lsp/LspModule.hpp>

#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

int main(int argc, char **argv) {
  return pegium::runLanguageServerMain<statemachine::StatemachineSharedServices>(
      argc, argv, "statemachine-lsp",
      statemachine::registerStatemachineLspServices);
}
