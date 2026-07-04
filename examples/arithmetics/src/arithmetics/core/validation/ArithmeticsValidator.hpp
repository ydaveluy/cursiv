#pragma once

#include <arithmetics/core/ast.hpp>

#include <string_view>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace arithmetics::validation {

namespace IssueCodes {

inline constexpr std::string_view ExpressionNormalizable =
    "arithmetics.expression-normalizable";

} // namespace IssueCodes

/// Inherits `pegium::DefaultCoreService` so checks can reach the language
/// services through `services.*` (parser, references, shared caches, ...).
class ArithmeticsValidator final : public pegium::DefaultCoreService {
public:
  using pegium::DefaultCoreService::DefaultCoreService;

  void checkDivByZero(
      const arithmetics::ast::BinaryExpression &expression,
      const pegium::validation::ValidationAcceptor &accept) const;
  void checkUniqueParameters(
      const arithmetics::ast::Definition &definition,
      const pegium::validation::ValidationAcceptor &accept) const;
  void checkNormalizable(
      const arithmetics::ast::Definition &definition,
      const pegium::validation::ValidationAcceptor &accept) const;
  void checkUniqueDefinitions(
      const arithmetics::ast::Module &module,
      const pegium::validation::ValidationAcceptor &accept) const;
  void checkFunctionRecursion(
      const arithmetics::ast::Module &module,
      const pegium::validation::ValidationAcceptor &accept) const;
  void checkMatchingParameters(
      const arithmetics::ast::FunctionCall &call,
      const pegium::validation::ValidationAcceptor &accept) const;
};

inline void registerValidationChecks(pegium::CoreServices &services,
                                     ArithmeticsValidator &validator) {
  auto &registry = *services.validation.validationRegistry;
  registry.registerChecks(
      {pegium::validation::ValidationRegistry::makeValidationCheck<
           &ArithmeticsValidator::checkDivByZero>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &ArithmeticsValidator::checkUniqueParameters>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &ArithmeticsValidator::checkNormalizable>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &ArithmeticsValidator::checkUniqueDefinitions>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &ArithmeticsValidator::checkFunctionRecursion>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &ArithmeticsValidator::checkMatchingParameters>(validator)});
}

} // namespace arithmetics::validation
