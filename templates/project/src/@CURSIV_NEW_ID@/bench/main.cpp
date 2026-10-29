// The @CURSIV_NEW_ID@ benchmark: it times parsing, document builds and editor
// features on the given files. Run it with `--help` to list the options.

#include "cursiv/bench/LanguageBench.hpp"
#include "@CURSIV_NEW_ID@/lsp/LspModule.hpp"

int main(int argc, char** argv) {
  return cursiv::bench::run_language<
      @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedServices,
      @CURSIV_NEW_ID@::register_lsp_services>(argc, argv);
}
