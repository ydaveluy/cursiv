#pragma once

#include <memory>
#include <string>

#include <requirements/lsp/LspServices.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace requirements {

/// LSP service overrides for the requirements language.
void installRequirementsLspModule(RequirementsServices &services);

/// LSP service overrides for the tests language.
void installTestsLspModule(TestsServices &services);

/// Builds the LSP-enabled requirements language services.
std::unique_ptr<RequirementsServices>
createRequirementsLspServices(const RequirementsSharedServices &sharedServices,
                              std::string languageId = "requirements-lang");

/// Builds the LSP-enabled tests language services.
std::unique_ptr<TestsServices>
createTestsLspServices(const RequirementsSharedServices &sharedServices,
                       std::string languageId = "tests-lang");

/// Aggregate of both languages' LSP-enabled services.
struct RequirementsAndTestsServices {
  std::unique_ptr<RequirementsServices> requirements;
  std::unique_ptr<TestsServices> tests;
};

/// Builds the LSP-enabled requirements and tests language services together.
RequirementsAndTestsServices createRequirementsAndTestsLspServices(
    const RequirementsSharedServices &sharedServices);

/// Registers the LSP-enabled services for both languages.
bool registerRequirementsLspServices(pegium::SharedServices &sharedServices);

} // namespace requirements
