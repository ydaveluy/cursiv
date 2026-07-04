#include <requirements/lsp/LspModule.hpp>

#include <utility>

#include <requirements/core/CoreModule.hpp>
#include <requirements/lsp/RequirementsFormatter.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>

namespace requirements {

void installRequirementsLspModule(RequirementsServices &services) {
  services.lsp.formatter = std::make_unique<RequirementsFormatter>(services);
}

void installTestsLspModule(TestsServices &services) {
  services.lsp.formatter = std::make_unique<TestsFormatter>(services);
}

std::unique_ptr<RequirementsServices>
createRequirementsLspServices(const RequirementsSharedServices &sharedServices,
                              std::string languageId) {
  auto services = pegium::makeDefaultServices<RequirementsServices>(
      sharedServices, std::move(languageId));
  installRequirementsCoreModule(*services);
  installRequirementsLspModule(*services);
  return services;
}

std::unique_ptr<TestsServices>
createTestsLspServices(const RequirementsSharedServices &sharedServices,
                       std::string languageId) {
  auto services = pegium::makeDefaultServices<TestsServices>(
      sharedServices, std::move(languageId));
  installTestsCoreModule(*services);
  installTestsLspModule(*services);
  return services;
}

RequirementsAndTestsServices createRequirementsAndTestsLspServices(
    const RequirementsSharedServices &sharedServices) {
  return {.requirements = createRequirementsLspServices(sharedServices),
          .tests = createTestsLspServices(sharedServices)};
}

bool registerRequirementsLspServices(pegium::SharedServices &sharedServices) {
  // The session container is the languages' shared type; recover it to build
  // the containers with their custom shared services.
  auto services = createRequirementsAndTestsLspServices(
      dynamic_cast<RequirementsSharedServices &>(sharedServices));
  sharedServices.serviceRegistry->registerServices(
      std::move(services.requirements));
  sharedServices.serviceRegistry->registerServices(std::move(services.tests));
  return true;
}

} // namespace requirements
