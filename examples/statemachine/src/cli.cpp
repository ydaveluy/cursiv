// The statemachine command line: the usual check, plus `generate FILE`: a
// model that becomes code.

#include "cursiv/cli.hpp"

#include <iostream>
#include <optional>
#include <string_view>

#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/workspace/Document.hpp"
#include "statemachine/CoreServices.hpp"
#include "statemachine/Generator.hpp"

namespace {

constexpr auto kRegister =
    &cursiv::registerLanguage<sm::StateMachineCoreServices,
                               sm::StateMachineSharedCoreServices,
                               sm::install_core_module>;

int generate(const cursiv::Document& document, std::string_view path,
             std::optional<std::string_view> destination) {
  if (cursiv::hasErrorDiagnostics(document)) {
    std::cerr << "There are validation errors:\n";
    cursiv::printDiagnostics(document, path, std::cerr);
    return cursiv::kDiagnostics;
  }
  const auto* model =
      cursiv::ast_cast<const sm::ast::Statemachine>(document.parseResult.value);
  if (!model) {
    std::cerr << "Unable to generate C++ for invalid statemachine.\n";
    return cursiv::kDiagnostics;
  }

  std::cout << sm::generate_cpp(*model, path, destination) << '\n';
  return cursiv::kClean;
}

}  // namespace

int main(int argc, char** argv) {
  return cursiv::runCli<sm::StateMachineSharedCoreServices>(
      argc, argv, kRegister,
      {{.name = "generate", .run = generate, .acceptsDestination = true}});
}
