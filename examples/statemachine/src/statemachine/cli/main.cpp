#include <statemachine/cli/Generator.hpp>
#include <statemachine/core/CoreModule.hpp>

#include <pegium/cli/CliUtils.hpp>

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct GenerateOptions {
  std::string fileName;
  std::optional<std::string> destination;
};

std::optional<GenerateOptions> parse_generate_args(int argc, char **argv) {
  if (argc < 3 || std::string_view(argv[1]) != "generate") {
    return std::nullopt;
  }

  GenerateOptions options{.fileName = argv[2]};
  for (int index = 3; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if ((arg == "-d" || arg == "--destination") && index + 1 < argc) {
      options.destination = std::string(argv[++index]);
      continue;
    }
    return std::nullopt;
  }
  return options;
}

int generate_cpp_cli(const GenerateOptions &options) {
  auto sharedServices =
      pegium::make_shared_services<statemachine::StatemachineSharedCoreServices>();
  auto &shared = *sharedServices;
  auto services = statemachine::createStatemachineCoreServices(shared);
  auto &statemachineServices = *services;
  shared.serviceRegistry->registerServices(std::move(services));

  auto document = pegium::build_document_from_path(
      options.fileName, statemachineServices);
  if (pegium::has_error_diagnostics(*document)) {
    std::cerr << "There are validation errors:\n";
    pegium::print_error_diagnostics(*document, std::cerr);
    return 2;
  }

  auto *model =
      pegium::ast_ptr_cast<statemachine::ast::Statemachine>(document->parseResult.value);
  if (model == nullptr) {
    std::cerr << "Unable to generate C++ for invalid statemachine.\n";
    return 2;
  }

  // The error-diagnostics gate above rejects unresolved references; the
  // generator throws (caught in main) for any remaining precondition.
  const auto outputPath = statemachine::generate_cpp(
      *model, options.fileName,
      options.destination.has_value()
          ? std::optional<std::string_view>(*options.destination)
          : std::nullopt);
  std::cout << "C++ code generated successfully: " << outputPath << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  const auto options = parse_generate_args(argc, argv);
  if (!options.has_value()) {
    std::cerr << "Usage: pegium-example-statemachine-cli generate <file.statemachine> [-d dir]\n";
    return 1;
  }

  try {
    return generate_cpp_cli(*options);
  } catch (const std::exception &error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return 3;
  }
}
