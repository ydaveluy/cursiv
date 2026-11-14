// The requirements command line: the usual check, plus `generate FILE`: an
// HTML coverage summary, which needs both languages of the workspace.

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/cli.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/LocalFileSystemProvider.hpp"
#include "requirements/Generator.hpp"
#include "requirements/CoreModule.hpp"

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
  req::register_core_services(shared);

  // The whole folder: a coverage summary is about the `.tst` files beside the
  // `.req`, so a build of the one file alone would report nothing covered. A
  // missing file is reported before its folder is scanned.
  const std::string path = std::filesystem::absolute(options.path).string();
  if (!std::filesystem::is_regular_file(path))
    throw cursiv::UsageError(path + " is not a regular file");
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

  // The summary is made of this model and of every test model: an error in
  // any of them is a summary that would say less than it seems to, so none
  // is written. Another requirement model of the folder is not read.
  std::vector<const req::ast::TestModel*> tests;
  std::vector<const cursiv::Document*> read{&document};
  for (const std::shared_ptr<cursiv::Document>& other : shared.documents->all())
    if (const auto* const test = cursiv::ast_cast<const req::ast::TestModel>(
            other->parseResult.value)) {
      tests.push_back(test);
      read.push_back(other.get());
    }
  bool errors = false;
  for (const cursiv::Document* const one : read) {
    if (!cursiv::has_error_diagnostics(*one)) continue;
    if (!errors) std::cerr << "There are validation errors:\n";
    errors = true;
    cursiv::print_diagnostics(
        *one, cursiv::file_uri_to_path(one->uri).value_or(one->uri), std::cerr);
  }
  if (errors) return cursiv::kDiagnostics;

  const std::optional<std::string_view> destination =
      options.destination.has_value()
          ? std::optional<std::string_view>(*options.destination)
          : std::nullopt;
  // Written first: a summary that cannot be written is not reported.
  const std::string summary =
      req::generate_summary(*model, tests, path, destination);
  std::cout << "Requirement coverage generated successfully: " << summary
            << '\n';
  return cursiv::kClean;
}

}  // namespace

int main(int argc, char** argv) {
  if (const std::optional<GenerateOptions> options =
          parse_generate_args(argc, argv))
    return cursiv::run_command([&] { return generate(*options); });
  return cursiv::run_cli<req::RequirementsSharedCoreServices>(
      argc, argv, &req::register_core_services);
}
