// The arithmetics command line: the usual check, plus `eval FILE`, since this
// language's documents have a value.

#include <iostream>
#include <string_view>

#include "arithmetics/Evaluator.hpp"
#include "arithmetics/CoreServices.hpp"
#include "cursiv/cli.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/workspace/LocalFileSystemProvider.hpp"

namespace {

constexpr auto kRegister =
    &cursiv::register_language<arith::ArithmeticsCoreServices,
                               arith::ArithmeticsSharedCoreServices,
                               arith::install_core_module>;

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string_view(argv[1]) == "eval")
    return cursiv::run_command([&] {
      arith::ArithmeticsSharedCoreServices shared;
      shared.fileSystemProvider =
          std::make_unique<cursiv::LocalFileSystemProvider>();
      cursiv::install_default_shared_core_services(shared);
      kRegister(shared);
      return arith::eval_file(argv[2], shared, std::cout, std::cerr);
    });
  return cursiv::run_cli<arith::ArithmeticsSharedCoreServices>(argc, argv,
                                                               kRegister);
}
