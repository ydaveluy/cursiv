#include <statemachine/core/CoreModule.hpp>

#include <utility>

#include <statemachine/core/StateMachineParser.hpp>
#include <statemachine/core/validation/StatemachineValidator.hpp>

// This is the single translation unit that includes the grammar header
// (StateMachineParser.hpp), so the heavy grammar template instantiation happens
// here once; the lsp module reaches the wiring through the declaration only.

namespace statemachine {

std::unique_ptr<const pegium::parser::Parser> createStatemachineParser() {
  return std::make_unique<const parser::StateMachineParser>();
}

void installStatemachineCoreModule(StatemachineCoreServices &services) {
  services.parser =
      std::make_unique<const parser::StateMachineParser>(services);
  services.languageMetaData.fileExtensions = {".statemachine"};
  services.validator =
      std::make_unique<validation::StatemachineValidator>(services);
  validation::registerValidationChecks(services, *services.validator);
}

std::unique_ptr<StatemachineCoreServices>
createStatemachineCoreServices(
    const StatemachineSharedCoreServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<StatemachineCoreServices>(
      sharedServices, std::move(languageId));
  installStatemachineCoreModule(*services);
  return services;
}

bool registerStatemachineCoreServices(
    pegium::SharedCoreServices &sharedServices) {
  // The workspace container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(
      createStatemachineCoreServices(
          dynamic_cast<StatemachineSharedCoreServices &>(sharedServices)));
  return true;
}

} // namespace statemachine
