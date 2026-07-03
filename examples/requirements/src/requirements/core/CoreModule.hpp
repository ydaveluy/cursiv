#pragma once

#include <memory>
#include <string>

#include <requirements/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace requirements {

/// Creates a standalone requirements parser.
std::unique_ptr<const pegium::parser::Parser> createRequirementsParser();

/// Creates a standalone tests-language parser.
std::unique_ptr<const pegium::parser::Parser> createTestsParser();

/// Wires the requirements-language core overrides onto a service container.
///
/// Takes the pegium core base and the requirements graft as two separate
/// references, so it is a plain function rather than a template: every container
/// is-a `pegium::CoreServices` and is-a `RequirementsAddedServices`, so the
/// headless and the LSP bundle both wire themselves with
/// `installRequirementsCoreModule(*services, *services)`.
void installRequirementsCoreModule(pegium::CoreServices &core,
                                   RequirementsAddedServices &added);

/// Wires the tests-language core overrides onto a service container; see
/// `installRequirementsCoreModule`.
void installTestsCoreModule(pegium::CoreServices &core,
                            TestsAddedServices &added);

/// Builds the core-only requirements language services.
std::unique_ptr<RequirementsCoreServices>
createRequirementsCoreServices(const pegium::SharedCoreServices &sharedServices,
                               std::string languageId = "requirements-lang");

/// Builds the core-only tests language services.
std::unique_ptr<TestsCoreServices>
createTestsCoreServices(const pegium::SharedCoreServices &sharedServices,
                        std::string languageId = "tests-lang");

/// Aggregate of both languages' core services.
struct RequirementsAndTestsCoreServices {
  std::unique_ptr<RequirementsCoreServices> requirements;
  std::unique_ptr<TestsCoreServices> tests;
};

/// Builds the requirements and tests core-only language services together.
RequirementsAndTestsCoreServices createRequirementsAndTestsCoreServices(
    const pegium::SharedCoreServices &sharedServices);

/// Registers the core-only services for both languages.
bool registerRequirementsCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace requirements
