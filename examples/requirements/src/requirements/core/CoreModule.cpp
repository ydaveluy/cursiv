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

void installRequirementsCoreModule(pegium::CoreServices &core,
                                   RequirementsAddedServices &added) {
  core.parser = std::make_unique<const parser::RequirementsParser>(core);
  core.languageMetaData.fileExtensions = {".req"};
  added.validator =
      std::make_unique<validation::RequirementsValidator>(core.shared);
  validation::registerRequirementsValidationChecks(core, *added.validator);
}

void installTestsCoreModule(pegium::CoreServices &core,
                            TestsAddedServices &added) {
  core.parser = std::make_unique<const parser::TestsParser>(core);
  core.languageMetaData.fileExtensions = {".tst"};
  added.validator = std::make_unique<validation::TestsValidator>();
  validation::registerTestsValidationChecks(core, *added.validator);
}

std::unique_ptr<RequirementsCoreServices>
createRequirementsCoreServices(const pegium::SharedCoreServices &sharedServices,
                               std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<RequirementsCoreServices>(
      sharedServices, std::move(languageId));
  installRequirementsCoreModule(*services, *services);
  return services;
}

std::unique_ptr<TestsCoreServices>
createTestsCoreServices(const pegium::SharedCoreServices &sharedServices,
                        std::string languageId) {
  auto services = pegium::makeDefaultCoreServices<TestsCoreServices>(
      sharedServices, std::move(languageId));
  installTestsCoreModule(*services, *services);
  return services;
}

RequirementsAndTestsCoreServices createRequirementsAndTestsCoreServices(
    const pegium::SharedCoreServices &sharedServices) {
  return {.requirements = createRequirementsCoreServices(sharedServices),
          .tests = createTestsCoreServices(sharedServices)};
}

bool registerRequirementsCoreServices(
    pegium::SharedCoreServices &sharedServices) {
  auto services = createRequirementsAndTestsCoreServices(sharedServices);
  sharedServices.serviceRegistry->registerServices(
      std::move(services.requirements));
  sharedServices.serviceRegistry->registerServices(std::move(services.tests));
  return true;
}

} // namespace requirements
