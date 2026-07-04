#pragma once

#include <domainmodel/core/ast.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace domainmodel::validation {

class DomainModelValidator final {
public:
  void checkEntityNameStartsWithCapital(
      const domainmodel::ast::Entity &entity,
      const pegium::validation::ValidationAcceptor &accept) const;
  void checkDataTypeNameStartsWithCapital(
      const domainmodel::ast::DataType &dataType,
      const pegium::validation::ValidationAcceptor &accept) const;
  void checkEntityDoesNotExtendItself(
      const domainmodel::ast::Entity &entity,
      const pegium::validation::ValidationAcceptor &accept) const;
};

inline void registerValidationChecks(pegium::CoreServices &services,
                                     DomainModelValidator &validator) {
  auto &registry = *services.validation.validationRegistry;
  registry.registerChecks(
      {pegium::validation::ValidationRegistry::makeValidationCheck<
           &DomainModelValidator::checkEntityNameStartsWithCapital>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &DomainModelValidator::checkDataTypeNameStartsWithCapital>(validator),
       pegium::validation::ValidationRegistry::makeValidationCheck<
           &DomainModelValidator::checkEntityDoesNotExtendItself>(validator)});
}

} // namespace domainmodel::validation
