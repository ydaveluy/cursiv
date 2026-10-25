// What this language installs over the generated module: what its author
// writes. Everything the grammar derives is in the generated one.

#include <memory>

#include "arithmetics/core/CoreServices.hpp"
#include "arithmetics/core/validation/ArithmeticsValidator.hpp"
#include "arithmetics/generated/Arith/Module.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"

namespace arith {

void install_core_module(ArithmeticsCoreServices& services) {
  arith::generated::Arith::install_generated_module(services);

  using V = ArithmeticsValidator;
  services.validator = std::make_unique<const V>(services);
  using R = cursiv::ValidationRegistry;
  const V& v = *services.validator;
  services.validationRegistry->registerBeforeDocument(
      [&v](const cursiv::Document& document, cursiv::ValidationAcceptor& accept,
           const cursiv::CancellationToken& token) {
        v.checkDivisionsByZero(document, accept, token);
      });
  services.validationRegistry->registerChecks(
      {R::makeValidationCheck<&V::checkUniqueParameters>(v),
       R::makeValidationCheck<&V::checkUniqueDefinitions>(v),
       R::makeValidationCheck<&V::checkMatchingParameters>(v),
       R::makeValidationCheck<&V::checkFunctionRecursion>(v),
       R::makeValidationCheck<&V::checkNormalizable>(v)});
}

}  // namespace arith
