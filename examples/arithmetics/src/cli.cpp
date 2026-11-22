// The arithmetics command line: the usual check, plus `eval FILE`, since this
// language's documents have a value.

#include "cursiv/cli.hpp"

#include <iostream>
#include <string_view>

#include "arithmetics/CoreServices.hpp"
#include "arithmetics/Evaluator.hpp"
#include "cursiv/services/CoreLanguage.hpp"

namespace {

constexpr auto kRegister =
    &cursiv::registerLanguage<arith::ArithmeticsCoreServices,
                               arith::ArithmeticsSharedCoreServices,
                               arith::install_core_module>;

int eval(const cursiv::Document& document, std::string_view path,
         std::optional<std::string_view>) {
  return arith::eval_document(document, path, std::cout, std::cerr);
}

}  // namespace

int main(int argc, char** argv) {
  return cursiv::runCli<arith::ArithmeticsSharedCoreServices>(
      argc, argv, kRegister, {{.name = "eval", .run = eval}});
}
