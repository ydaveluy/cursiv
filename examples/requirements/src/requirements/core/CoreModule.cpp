// What this language installs over the generated module, and the two ways to
// obtain a container of it: built here, or registered in a workspace. The
// grammar is not compiled here -- the generated module carries it.

#include "requirements/core/CoreModule.hpp"

#include <memory>
#include <string>
#include <utility>

#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "requirements/core/validation/RequirementsValidator.hpp"
#include "requirements/core/validation/TestsValidator.hpp"
#include "requirements/generated/Requirements/Module.hpp"
#include "requirements/generated/Tests/Module.hpp"

namespace req {

void install_requirements_core_module(RequirementsCoreServices& services) {
  req::generated::Requirements::install_generated_module(services);

  using V = RequirementsValidator;
  services.requirementsValidator = std::make_unique<const V>(services);
  using R = cursiv::ValidationRegistry;
  const V& v = *services.requirementsValidator;
  services.validationRegistry->registerChecks(
      {R::makeValidationCheck<&V::checkRequirementNameContainsANumber>(v),
       R::makeValidationCheck<&V::checkRequirementIsCoveredByATest>(v)});
}

void install_tests_core_module(RequirementsCoreServices& services) {
  req::generated::Tests::install_generated_module(services);

  using V = TestsValidator;
  services.testsValidator = std::make_unique<const V>(services);
  using R = cursiv::ValidationRegistry;
  const V& v = *services.testsValidator;
  services.validationRegistry->registerChecks(
      {R::makeValidationCheck<&V::checkTestNameContainsANumber>(v),
       R::makeValidationCheck<
           &V::checkTestReferencesOnlyEnvironmentsAlsoReferencedInSomeRequirement>(
           v)});
}

std::unique_ptr<RequirementsCoreServices> create_requirements_core_services(
    RequirementsSharedCoreServices& sharedServices, std::string languageId) {
  auto services = cursiv::make_default_core_services<RequirementsCoreServices>(
      sharedServices, std::move(languageId));
  install_requirements_core_module(*services);
  return services;
}

std::unique_ptr<RequirementsCoreServices> create_tests_core_services(
    RequirementsSharedCoreServices& sharedServices, std::string languageId) {
  auto services = cursiv::make_default_core_services<RequirementsCoreServices>(
      sharedServices, std::move(languageId));
  install_tests_core_module(*services);
  return services;
}

RequirementsCoreLanguage create_core_services(
    std::unique_ptr<cursiv::FileSystemProvider> files) {
  auto shared = std::make_unique<RequirementsSharedCoreServices>();
  shared->fileSystemProvider =
      files != nullptr ? std::move(files)
                       : std::make_unique<cursiv::EmptyFileSystemProvider>();
  cursiv::install_default_shared_core_services(*shared);
  auto requirements = create_requirements_core_services(*shared);
  auto tests = create_tests_core_services(*shared);
  RequirementsCoreServices& one = *requirements;
  RequirementsCoreServices& other = *tests;
  shared->serviceRegistry->registerServices(std::move(requirements));
  shared->serviceRegistry->registerServices(std::move(tests));
  return {std::move(shared), one, other};
}

bool register_core_services(cursiv::SharedCoreServices& sharedServices) {
  auto& shared = dynamic_cast<RequirementsSharedCoreServices&>(sharedServices);
  sharedServices.serviceRegistry->registerServices(
      create_requirements_core_services(shared));
  sharedServices.serviceRegistry->registerServices(
      create_tests_core_services(shared));
  return true;
}

}  // namespace req
