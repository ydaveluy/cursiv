#include <statemachine/lsp/LspModule.hpp>

#include <utility>

#include <statemachine/core/CoreModule.hpp>
#include <statemachine/lsp/StatemachineFormatter.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>

namespace statemachine {

void installStatemachineLspModule(StatemachineServices &services) {
  services.lsp.formatter = std::make_unique<StatemachineFormatter>(services);
}

std::unique_ptr<StatemachineServices>
createStatemachineLspServices(const StatemachineSharedServices &sharedServices,
                              std::string languageId) {
  auto services = pegium::makeDefaultServices<StatemachineServices>(
      sharedServices, std::move(languageId));
  installStatemachineCoreModule(*services);
  installStatemachineLspModule(*services);
  return services;
}

bool registerStatemachineLspServices(pegium::SharedServices &sharedServices) {
  // The session container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(
      createStatemachineLspServices(
          dynamic_cast<StatemachineSharedServices &>(sharedServices)));
  return true;
}

} // namespace statemachine
