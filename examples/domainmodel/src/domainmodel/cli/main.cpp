// The domainmodel command line: the usual verdict, plus `generate FILE` -- one
// Java class per entity, in the directory a package spells.

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/services/CoreLanguage.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "domainmodel/cli/Generator.hpp"
#include "domainmodel/core/CoreServices.hpp"

namespace {

constexpr auto kRegister =
    &cursiv::register_language<dm::DomainModelCoreServices,
                               dm::DomainModelSharedCoreServices,
                               dm::install_core_module>;

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
  dm::DomainModelSharedCoreServices shared;
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
  const auto* const model =
      cursiv::ast_cast<const dm::ast::DomainModel>(document.parseResult.value);
  if (model == nullptr) {
    std::cerr << "Unable to generate Java for an invalid domain model.\n";
    return cursiv::kDiagnostics;
  }

  const std::optional<std::string_view> destination =
      options.destination.has_value()
          ? std::optional<std::string_view>(*options.destination)
          : std::nullopt;
  std::cout << dm::generate_java(*model, options.path, destination) << '\n';
  return cursiv::kClean;
}

}  // namespace

int main(int argc, char** argv) {
  if (const std::optional<GenerateOptions> options =
          parse_generate_args(argc, argv))
    return generate(*options);
  return cursiv::run_cli<dm::DomainModelSharedCoreServices>(argc, argv,
                                                            kRegister);
}
