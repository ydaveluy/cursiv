// The statemachine language server: one language, the standard transport.

#include "cursiv/lsp/runtime/LanguageServerMain.hpp"
#include "statemachine/lsp/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::run_language_server_main<sm::StateMachineSharedServices>(
      argc, argv, "statemachine-lsp", &sm::register_lsp_services);
}
