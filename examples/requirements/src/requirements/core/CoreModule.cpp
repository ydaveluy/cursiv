#include <requirements/core/CoreModule.hpp>

#include <utility>

#include <requirements/core/RequirementsParser.hpp>
#include <requirements/core/validation/RequirementsValidator.hpp>
#include <requirements/core/validation/TestsValidator.hpp>

// This is the single translation unit that includes the grammar header
// (RequirementsParser.hpp), so the heavy grammar template instantiations for
// both languages happen here once; the lsp module reaches the wiring through the
// declarations only.

namespace requirements {

std::unique_ptr<const pegium::parser::Parser> createRequirementsParser() {
  return std::make_unique<const parser::RequirementsParser>();
}

std::unique_ptr<const pegium::parser::Parser> createTestsParser() {
  return std::make_unique<const parser::TestsParser>();
}

void installRequirementsCoreModule(RequirementsCoreServices &services) {
  services.parser = std::make_unique<const parser::RequirementsParser>(services);
  services.languageMetaData.fileExtensions = {".req"};
  services.validator =
      std::make_unique<validation::RequirementsValidator>(services);
  validation::registerRequirementsValidationChecks(services,
                                                   *services.validator);
}

void installTestsCoreModule(TestsCoreServices &services) {
  services.parser = std::make_unique<const parser::TestsParser>(services);
  services.languageMetaData.fileExtensions = {".tst"};
  services.validator = std::make_unique<validation::TestsValidator>(services);
  validation::registerTestsValidationChecks(services, *services.validator);
}

std::unique_ptr<RequirementsCoreServices>
createRequirementsCoreServices(
    const RequirementsSharedCoreServices &sharedServices,
    std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<RequirementsCoreServices>(
      sharedServices, std::move(languageId));
  installRequirementsCoreModule(*services);
  return services;
}

std::unique_ptr<TestsCoreServices>
createTestsCoreServices(const RequirementsSharedCoreServices &sharedServices,
                        std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<TestsCoreServices>(
      sharedServices, std::move(languageId));
  installTestsCoreModule(*services);
  return services;
}

RequirementsAndTestsCoreServices createRequirementsAndTestsCoreServices(
    const RequirementsSharedCoreServices &sharedServices) {
  return {.requirements = createRequirementsCoreServices(sharedServices),
          .tests = createTestsCoreServices(sharedServices)};
}

bool registerRequirementsCoreServices(
    pegium::SharedCoreServices &sharedServices) {
  // The workspace container is the languages' shared type; recover it to build
  // the containers with their custom shared services.
  auto services = createRequirementsAndTestsCoreServices(
      dynamic_cast<RequirementsSharedCoreServices &>(sharedServices));
  sharedServices.serviceRegistry->registerServices(
      std::move(services.requirements));
  sharedServices.serviceRegistry->registerServices(std::move(services.tests));
  return true;
}

} // namespace requirements
