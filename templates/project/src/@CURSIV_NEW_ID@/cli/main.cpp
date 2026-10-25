// The @CURSIV_NEW_ID@ command line: parse one file, link it, validate it, and
// answer on the exit code -- 0 clean, 1 diagnostics, 2 misuse, 3 fatal.

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "@CURSIV_NEW_ID@/core/CoreServices.hpp"

// A language is an installer: the three type arguments are all that
// separates one from another, so `run_cli` itself is compiled once for
// every language there is and no grammar reaches it.
constexpr auto kRegister =
    &cursiv::register_language<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@CoreServices,
                           @CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices,
                           @CURSIV_NEW_ID@::install_core_module>;

int main(int argc, char** argv) {
  return cursiv::run_cli<@CURSIV_NEW_ID@::@CURSIV_NEW_CLASS@SharedCoreServices>(
      argc, argv, kRegister);
}
