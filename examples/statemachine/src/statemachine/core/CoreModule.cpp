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

void installStatemachineCoreModule(pegium::CoreServices &core,
                                   StatemachineAddedServices &added) {
  core.parser = std::make_unique<const parser::StateMachineParser>(core);
  core.languageMetaData.fileExtensions = {".statemachine"};
  added.validator = std::make_unique<validation::StatemachineValidator>();
  validation::registerValidationChecks(core, *added.validator);
}

std::unique_ptr<StatemachineCoreServices>
createStatemachineCoreServices(const pegium::SharedCoreServices &sharedServices,
                               std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<StatemachineCoreServices>(
      sharedServices, std::move(languageId));
  installStatemachineCoreModule(*services, *services);
  return services;
}

bool registerStatemachineCoreServices(
    pegium::SharedCoreServices &sharedServices) {
  sharedServices.serviceRegistry->registerServices(
      createStatemachineCoreServices(sharedServices));
  return true;
}

} // namespace statemachine
