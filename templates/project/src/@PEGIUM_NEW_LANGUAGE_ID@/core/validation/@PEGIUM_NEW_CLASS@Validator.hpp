#pragma once

#include <@PEGIUM_NEW_LANGUAGE_ID@/core/ast.hpp>

#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/validation/ValidationAcceptor.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace @PEGIUM_NEW_LANGUAGE_ID@::validation {

/// Language validator. It inherits `pegium::DefaultCoreService` so checks can
/// reach the language services through `services.*` (parser, references, shared
/// caches, ...) as the language grows — no refactor needed the first time a
/// check needs a sibling service or a per-workspace cache.
class @PEGIUM_NEW_CLASS@Validator final : public pegium::DefaultCoreService {
public:
  using pegium::DefaultCoreService::DefaultCoreService;

  void checkPersonNameStartsWithCapital(
      const ast::Person &person,
      const pegium::validation::ValidationAcceptor &accept) const;
};

inline void registerValidationChecks(pegium::CoreServices &services,
                                     @PEGIUM_NEW_CLASS@Validator &validator) {
  auto &registry = *services.validation.validationRegistry;
  registry.registerChecks(
      {pegium::validation::ValidationRegistry::makeValidationCheck<
          &@PEGIUM_NEW_CLASS@Validator::checkPersonNameStartsWithCapital>(
          validator)});
}

} // namespace @PEGIUM_NEW_LANGUAGE_ID@::validation
