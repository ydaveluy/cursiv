#include <requirements/cli/CliUtils.hpp>
#include <requirements/cli/Generator.hpp>
#include <requirements/core/CoreModule.hpp>

#include <pegium/cli/CliUtils.hpp>

#include <filesystem>
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

int generate_cli(const GenerateOptions &options) {
  auto sharedServices = pegium::make_shared_services<
      requirements::RequirementsSharedCoreServices>();
  auto &shared = *sharedServices;
  auto services = requirements::createRequirementsAndTestsCoreServices(shared);
  auto &requirementsServices = *services.requirements;
  shared.serviceRegistry->registerServices(std::move(services.requirements));
  shared.serviceRegistry->registerServices(std::move(services.tests));

  const auto absoluteInputPath = std::filesystem::absolute(options.fileName);
  const auto extracted = requirements::extract_requirement_model_with_test_models(
      absoluteInputPath.string(), requirementsServices);

  for (const auto &document : extracted.documents) {
    if (pegium::has_error_diagnostics(*document)) {
      std::cerr << "There are validation errors:\n";
      pegium::print_error_diagnostics(*document, std::cerr);
      return 2;
    }
  }

  const auto outputPath = requirements::generate_summary(
      *extracted.requirementModel, extracted.testModels, absoluteInputPath.string(),
      options.destination.has_value()
          ? std::optional<std::string_view>(*options.destination)
          : std::nullopt);
  std::cout << "Requirement coverage generated successfully: " << outputPath
            << '\n';
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  const auto options = parse_generate_args(argc, argv);
  if (!options.has_value()) {
    std::cerr
        << "Usage: pegium-example-requirements-cli generate <file.req> [-d dir]\n";
    return 1;
  }

  try {
    return generate_cli(*options);
  } catch (const std::invalid_argument &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return 3;
  }
}
