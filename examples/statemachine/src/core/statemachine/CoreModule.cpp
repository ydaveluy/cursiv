// What this language installs over the generated module: what its author
// writes. Everything the grammar derives is in the generated one.

#include <memory>

#include "cursiv/validation/ValidationRegistry.hpp"
#include "statemachine/CoreServices.hpp"
#include "statemachine/validation/StateMachineValidator.hpp"
#include "statemachine/generated/SM/Module.hpp"

namespace sm {

void install_core_module(StateMachineCoreServices& services) {
  sm::generated::SM::install_generated_module(services);

  using V = StateMachineValidator;
  services.validator = std::make_unique<const V>(services);
  using R = cursiv::ValidationRegistry;
  const V& v = *services.validator;
  services.validationRegistry->registerChecks(
      {R::makeValidationCheck<&V::checkStateNameStartsWithCapital>(v),
       R::makeValidationCheck<&V::checkUniqueStatesAndEvents>(v),
       R::makeValidationCheck<&V::checkNamesAreNotKeywords>(v)});
}

}  // namespace sm
