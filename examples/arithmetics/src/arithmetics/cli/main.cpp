#include <arithmetics/core/CoreModule.hpp>

#include <pegium/cli/CliUtils.hpp>

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <arithmetics/cli/Evaluator.hpp>

namespace {

struct EvalOptions {
  std::string fileName;
};

std::optional<EvalOptions> parse_eval_args(int argc, char **argv) {
  if (argc != 3 || std::string_view(argv[1]) != "eval") {
    return std::nullopt;
  }
  return EvalOptions{.fileName = argv[2]};
}

int eval_cli(const EvalOptions &options) {
  auto sharedServices =
      pegium::make_shared_services<arithmetics::ArithmeticsSharedCoreServices>();
  auto &shared = *sharedServices;
  auto services = arithmetics::createArithmeticsCoreServices(shared);
  auto &arithmeticsServices = *services;
  shared.serviceRegistry->registerServices(std::move(services));
  return arithmetics::eval_file(options.fileName, arithmeticsServices);
}

} // namespace

int main(int argc, char **argv) {
  const auto options = parse_eval_args(argc, argv);
  if (!options.has_value()) {
    std::cerr << "Usage: pegium-example-arithmetics-cli eval <file.calc>\n";
    return 1;
  }

  try {
    return eval_cli(*options);
  } catch (const std::exception &error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return 3;
  }
}
