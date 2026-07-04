#include <arithmetics/lsp/LspModule.hpp>

#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

int main(int argc, char **argv) {
  return pegium::runLanguageServerMain<arithmetics::ArithmeticsSharedServices>(
      argc, argv, "arithmetics-lsp",
      arithmetics::registerArithmeticsLspServices);
}
