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

void installArithmeticsCoreModule(pegium::CoreServices &core,
                                  ArithmeticsAddedServices &added) {
  core.parser = std::make_unique<const parser::ArithmeticParser>(core);
  core.languageMetaData.fileExtensions = {".calc"};
  added.validator = std::make_unique<validation::ArithmeticsValidator>();
  validation::registerValidationChecks(core, *added.validator);
}

std::unique_ptr<ArithmeticsCoreServices>
createArithmeticsCoreServices(const pegium::SharedCoreServices &sharedServices,
                              std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<ArithmeticsCoreServices>(
      sharedServices, std::move(languageId));
  installArithmeticsCoreModule(*services, *services);
  return services;
}

bool registerArithmeticsCoreServices(pegium::SharedCoreServices &sharedServices) {
  sharedServices.serviceRegistry->registerServices(
      createArithmeticsCoreServices(sharedServices));
  return true;
}

} // namespace arithmetics
