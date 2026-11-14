// The statemachine command line: the usual check, plus `generate FILE`: a
// model that becomes code.

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/cli.hpp"
#include "cursiv/services/CoreLanguage.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/LocalFileSystemProvider.hpp"
#include "statemachine/Generator.hpp"
#include "statemachine/CoreServices.hpp"

namespace {

constexpr auto kRegister =
    &cursiv::register_language<sm::StateMachineCoreServices,
                               sm::StateMachineSharedCoreServices,
                               sm::install_core_module>;

struct GenerateOptions {
  std::string path;
  std::optional<std::string> destination;
};

std::optional<GenerateOptions> parse_generate_args(int argc, char** argv) {
  if (argc < 3 || std::string_view(argv[1]) != "generate") return std::nullopt;
  GenerateOptions options{.path = argv[2], .destination = std::nullopt};
  for (int at = 3; at < argc; ++at) {
    const std::string_view arg(argv[at]);
    if ((arg == "-d" || arg == "--destination") && at + 1 < argc) {
      options.destination = std::string(argv[++at]);
      continue;
    }
    return std::nullopt;
  }
  return options;
}

int generate(const GenerateOptions& options) {
  sm::StateMachineSharedCoreServices shared;
  shared.fileSystemProvider =
      std::make_unique<cursiv::LocalFileSystemProvider>();
  cursiv::install_default_shared_core_services(shared);
  kRegister(shared);

  // The file alone, not its folder: the generated program declares only this
  // machine's states and events, so a name from a neighbour is unresolved.
  const cursiv::Document& document =
      cursiv::build_document_from_path(options.path, shared);
  if (cursiv::has_error_diagnostics(document)) {
    std::cerr << "There are validation errors:\n";
    cursiv::print_diagnostics(document, options.path, std::cerr);
    return cursiv::kDiagnostics;
  }
  const cursiv::AstNode* const root = document.parseResult.value;
  if (root == nullptr
      || root->type != &cursiv::ast_type_tag<sm::ast::Statemachine>) {
    std::cerr << "Unable to generate C++ for invalid statemachine.\n";
    return cursiv::kDiagnostics;
  }

  const std::optional<std::string_view> destination =
      options.destination.has_value()
          ? std::optional<std::string_view>(*options.destination)
          : std::nullopt;
  std::cout << sm::generate_cpp(
      static_cast<const sm::ast::Statemachine&>(*root), options.path,
      destination)
            << '\n';
  return cursiv::kClean;
}

}  // namespace

int main(int argc, char** argv) {
  if (const std::optional<GenerateOptions> options =
          parse_generate_args(argc, argv))
    return cursiv::run_command([&] { return generate(*options); });
  return cursiv::run_cli<sm::StateMachineSharedCoreServices>(argc, argv,
                                                             kRegister);
}
