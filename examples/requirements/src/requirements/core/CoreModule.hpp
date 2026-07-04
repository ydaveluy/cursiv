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

/// Wires the requirements-language core overrides. The LSP path reuses it.
void installRequirementsCoreModule(RequirementsCoreServices &services);

/// Wires the tests-language core overrides. The LSP path reuses it.
void installTestsCoreModule(TestsCoreServices &services);

/// Builds the core-only requirements language services.
std::unique_ptr<RequirementsCoreServices>
createRequirementsCoreServices(
    const RequirementsSharedCoreServices &sharedServices,
    std::string languageId = "requirements-lang");

/// Builds the core-only tests language services.
std::unique_ptr<TestsCoreServices>
createTestsCoreServices(const RequirementsSharedCoreServices &sharedServices,
                        std::string languageId = "tests-lang");

/// Aggregate of both languages' core services.
struct RequirementsAndTestsCoreServices {
  std::unique_ptr<RequirementsCoreServices> requirements;
  std::unique_ptr<TestsCoreServices> tests;
};

/// Builds the requirements and tests core-only language services together.
RequirementsAndTestsCoreServices createRequirementsAndTestsCoreServices(
    const RequirementsSharedCoreServices &sharedServices);

/// Registers the core-only services for both languages.
bool registerRequirementsCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace requirements
