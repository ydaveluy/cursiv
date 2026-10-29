// The @CURSIV_NEW_ID@ command line: it parses, links and validates a file and
// prints its diagnostics. Run it with `--help` to list the options. It returns
// 0 for no errors, 1 for document errors, 2 for invalid arguments or paths, and
// 3 for an internal or output error.

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

// Adds the language to the workspace that the command line creates.
constexpr auto kRegister =
    &cursiv::register_language<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@CoreServices,
                           @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices,
                           @CURSIV_NEW_ID@::install_core_module>;

int main(int argc, char** argv) {
  return cursiv::run_cli<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices>(
      argc, argv, kRegister);
}
