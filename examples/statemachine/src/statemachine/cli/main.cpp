// The statemachine command line: the usual verdict, plus `generate FILE` --
// what this example is for, a model that becomes code.

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "statemachine/cli/Generator.hpp"
#include "statemachine/core/CoreServices.hpp"

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
  if (!kRegister(shared)) return cursiv::kFatal;

  // The file's folder first: a model that names a declaration from the file
  // beside it is not invalid, it is unscanned -- and a generator that refused
  // it would refuse the example this one ships with.
  cursiv::initialize_root(
      std::filesystem::path(options.path).parent_path().string(), shared);
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
    return generate(*options);
  return cursiv::run_cli<sm::StateMachineSharedCoreServices>(argc, argv,
                                                             kRegister);
}
