// The arithmetics command line: the usual verdict, plus `eval FILE` -- which is
// what this example is for, a language whose documents have a value.

#include <string_view>

#include "arithmetics/cli/Evaluator.hpp"
#include "arithmetics/core/CoreServices.hpp"
#include "cursiv/cli/cli.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"

namespace {

constexpr auto kRegister =
    &cursiv::register_language<arith::ArithmeticsCoreServices,
                               arith::ArithmeticsSharedCoreServices,
                               arith::install_core_module>;

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string_view(argv[1]) == "eval") {
    arith::ArithmeticsSharedCoreServices shared;
    shared.fileSystemProvider =
        std::make_unique<cursiv::LocalFileSystemProvider>();
    cursiv::install_default_shared_core_services(shared);
    if (!kRegister(shared)) return cursiv::kFatal;
    return arith::eval_file(argv[2], shared);
  }
  return cursiv::run_cli<arith::ArithmeticsSharedCoreServices>(argc, argv,
                                                               kRegister);
}
