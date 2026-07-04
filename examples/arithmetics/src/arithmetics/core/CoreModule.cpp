#include <arithmetics/core/CoreModule.hpp>

#include <utility>

#include <arithmetics/core/ArithmeticParser.hpp>
#include <arithmetics/core/validation/ArithmeticsValidator.hpp>

// This is the single translation unit that includes the grammar header
// (ArithmeticParser.hpp), so the heavy grammar template instantiation happens
// here once; the lsp module reaches the wiring through the declaration only.

namespace arithmetics {

std::unique_ptr<const pegium::parser::Parser> createArithmeticsParser() {
  return std::make_unique<const parser::ArithmeticParser>();
}

void installArithmeticsCoreModule(ArithmeticsCoreServices &services) {
  services.parser = std::make_unique<const parser::ArithmeticParser>(services);
  services.languageMetaData.fileExtensions = {".calc"};
  services.validator =
      std::make_unique<validation::ArithmeticsValidator>(services);
  validation::registerValidationChecks(services, *services.validator);
}

std::unique_ptr<ArithmeticsCoreServices>
createArithmeticsCoreServices(
    const ArithmeticsSharedCoreServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<ArithmeticsCoreServices>(
      sharedServices, std::move(languageId));
  installArithmeticsCoreModule(*services);
  return services;
}

bool registerArithmeticsCoreServices(pegium::SharedCoreServices &sharedServices) {
  // The workspace container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(createArithmeticsCoreServices(
      dynamic_cast<ArithmeticsSharedCoreServices &>(sharedServices)));
  return true;
}

} // namespace arithmetics
