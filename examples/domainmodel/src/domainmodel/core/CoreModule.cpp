// What this language installs over the generated module: what its author
// writes. Everything the grammar derives is in the generated one.

#include <memory>

#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "domainmodel/core/CoreServices.hpp"
#include "domainmodel/core/references/DomainModelScopeComputation.hpp"
#include "domainmodel/core/references/DomainModelScopeProvider.hpp"
#include "domainmodel/core/references/QualifiedNameProvider.hpp"
#include "domainmodel/core/validation/DomainModelValidator.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"

namespace dm {

void install_core_module(DomainModelCoreServices& services) {
  dm::generated::DomainModel::install_generated_module(services);
  services.qualifiedNameProvider =
      std::make_unique<const QualifiedNameProvider>(services.shared);
  services.scopeComputation =
      std::make_unique<DomainModelScopeComputation>(services);
  // The language is case-insensitive: references match symbols whatever the
  // case, the spelling stays the author's.
  services.scopeProvider = std::make_unique<DomainModelScopeProvider>(services);

  using V = DomainModelValidator;
  services.validator = std::make_unique<const V>(services);
  using R = cursiv::ValidationRegistry;
  const V& v = *services.validator;
  services.validationRegistry->registerChecks(
      {R::makeValidationCheck<&V::checkEntityNameStartsWithCapital>(v),
       R::makeValidationCheck<&V::checkDataTypeNameStartsWithCapital>(v),
       R::makeValidationCheck<&V::checkEntityDoesNotExtendItself>(v)});
}

}  // namespace dm
