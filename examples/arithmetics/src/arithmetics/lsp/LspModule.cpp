#include <arithmetics/lsp/LspModule.hpp>

#include <utility>

#include <arithmetics/core/CoreModule.hpp>
#include <arithmetics/lsp/ArithmeticsCodeActionProvider.hpp>
#include <arithmetics/lsp/ArithmeticsFormatter.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>

namespace arithmetics {

void installArithmeticsLspModule(ArithmeticsServices &services) {
  services.lsp.codeActionProvider =
      std::make_unique<ArithmeticsCodeActionProvider>();
  services.lsp.formatter = std::make_unique<ArithmeticsFormatter>(services);
}

std::unique_ptr<ArithmeticsServices>
createArithmeticsLspServices(const ArithmeticsSharedServices &sharedServices,
                             std::string languageId) {
  auto services = pegium::makeDefaultServices<ArithmeticsServices>(
      sharedServices, std::move(languageId));
  installArithmeticsCoreModule(*services);
  installArithmeticsLspModule(*services);
  return services;
}

bool registerArithmeticsLspServices(pegium::SharedServices &sharedServices) {
  // The session container is the language's own shared type; recover it to
  // build the container with its custom shared services.
  sharedServices.serviceRegistry->registerServices(createArithmeticsLspServices(
      dynamic_cast<ArithmeticsSharedServices &>(sharedServices)));
  return true;
}

} // namespace arithmetics
