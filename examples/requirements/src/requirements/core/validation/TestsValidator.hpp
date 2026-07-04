#pragma once

#include <requirements/core/ast.hpp>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace requirements::validation {

/// Inherits `pegium::DefaultCoreService` so checks can reach the language
/// services through `services.*` (parser, references, shared caches, ...).
class TestsValidator final : public pegium::DefaultCoreService {
public:
  using pegium::DefaultCoreService::DefaultCoreService;

  void checkTestNameContainsANumber(
      const requirements::ast::Test &test,
      const pegium::validation::ValidationAcceptor &accept) const;

  void checkTestReferencesOnlyEnvironmentsAlsoReferencedInSomeRequirement(
      const requirements::ast::Test &test,
      const pegium::validation::ValidationAcceptor &accept) const;
};

inline void registerTestsValidationChecks(pegium::CoreServices &services,
                                          TestsValidator &validator) {
  auto &registry = *services.validation.validationRegistry;
  registry.registerChecks(
      {pegium::validation::ValidationRegistry::makeValidationCheck<
           &TestsValidator::checkTestNameContainsANumber>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &TestsValidator::
               checkTestReferencesOnlyEnvironmentsAlsoReferencedInSomeRequirement>(
           validator)});
}

} // namespace requirements::validation
