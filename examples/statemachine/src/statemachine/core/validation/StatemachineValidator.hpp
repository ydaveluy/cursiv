#pragma once

#include <statemachine/core/ast.hpp>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace statemachine::validation {

/// Inherits `pegium::DefaultCoreService` so checks can reach the language
/// services through `services.*` (parser, references, shared caches, ...).
class StatemachineValidator final : public pegium::DefaultCoreService {
public:
  using pegium::DefaultCoreService::DefaultCoreService;

  void checkStateNameStartsWithCapital(
      const statemachine::ast::State &state,
      const pegium::validation::ValidationAcceptor &accept) const;

  void checkUniqueStatesAndEvents(
      const statemachine::ast::Statemachine &model,
      const pegium::validation::ValidationAcceptor &accept) const;
};

inline void registerValidationChecks(pegium::CoreServices &services,
                                     StatemachineValidator &validator) {
  auto &registry = *services.validation.validationRegistry;
  registry.registerChecks(
      {pegium::validation::ValidationRegistry::makeValidationCheck<
           &StatemachineValidator::checkStateNameStartsWithCapital>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &StatemachineValidator::checkUniqueStatesAndEvents>(validator)});
}

} // namespace statemachine::validation
