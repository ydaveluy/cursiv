// The requirements command line: the usual verdict, plus `generate FILE` -- an
// HTML coverage summary, which needs both languages of the workspace.

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "requirements/cli/Generator.hpp"
#include "requirements/core/CoreModule.hpp"

namespace {

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
  req::RequirementsSharedCoreServices shared;
  shared.fileSystemProvider =
      std::make_unique<cursiv::LocalFileSystemProvider>();
  cursiv::install_default_shared_core_services(shared);
  if (!req::register_core_services(shared)) return cursiv::kFatal;

  // The whole folder: a coverage summary is about the `.tst` files beside the
  // `.req`, so a build of the one file alone would report nothing covered.
  const std::string path = std::filesystem::absolute(options.path).string();
  cursiv::initialize_root(std::filesystem::path(path).parent_path().string(),
                          shared);
  const cursiv::Document& document =
      cursiv::build_document_from_path(path, shared);

  const auto* const model = cursiv::ast_cast<const req::ast::RequirementModel>(
      document.parseResult.value);
  if (model == nullptr) {
    std::cerr << "Unable to generate a summary for an invalid requirement "
                 "model.\n";
    return cursiv::kDiagnostics;
  }

  std::vector<const req::ast::TestModel*> tests;
  for (const std::shared_ptr<cursiv::Document>& other : shared.documents->all())
    if (const auto* const test = cursiv::ast_cast<const req::ast::TestModel>(
            other->parseResult.value))
      tests.push_back(test);

  const std::optional<std::string_view> destination =
      options.destination.has_value()
          ? std::optional<std::string_view>(*options.destination)
          : std::nullopt;
  std::cout << "Requirement coverage generated successfully: "
            << req::generate_summary(*model, tests, path, destination) << '\n';
  return cursiv::kClean;
}

}  // namespace

int main(int argc, char** argv) {
  if (const std::optional<GenerateOptions> options =
          parse_generate_args(argc, argv))
    return generate(*options);
  return cursiv::run_cli<req::RequirementsSharedCoreServices>(
      argc, argv, &req::register_core_services);
}
