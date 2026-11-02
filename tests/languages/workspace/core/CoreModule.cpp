// What this language installs over the generated module: what its author
// writes. Everything the grammar derives is in the generated one.

#include <memory>

#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "support/WorkspaceModule.hpp"
#include "workspace/core/CoreServices.hpp"
#include "workspace/core/references/WorkspaceScopeComputation.hpp"
#include "workspace/core/references/WorkspaceScopeProvider.hpp"
#include "workspace/core/references/QualifiedNameProvider.hpp"
#include "workspace/core/validation/WorkspaceValidator.hpp"
#include "workspace/generated/Workspace/Module.hpp"

namespace ws {

void install_core_module(WorkspaceCoreServices& services) {
  ws::generated::Workspace::install_generated_module(services);
  services.qualifiedNameProvider =
      std::make_unique<const QualifiedNameProvider>(services.shared);
  services.scopeComputation =
      std::make_unique<WorkspaceScopeComputation>(services);
  // The language is case-insensitive: references match symbols whatever the
  // case, the spelling stays the author's.
  services.scopeProvider = std::make_unique<WorkspaceScopeProvider>(services);

  using V = WorkspaceValidator;
  services.validator = std::make_unique<const V>(services);
  using R = cursiv::ValidationRegistry;
  const V& v = *services.validator;
  services.validationRegistry->registerChecks(
      {R::makeValidationCheck<&V::checkEntityNameStartsWithCapital>(v),
       R::makeValidationCheck<&V::checkDataTypeNameStartsWithCapital>(v),
       R::makeValidationCheck<&V::checkEntityDoesNotExtendItself>(v)});
}

}  // namespace ws

namespace support {

std::unique_ptr<cursiv::Parser> create_workspace_parser() {
  return ws::generated::Workspace::create_parser();
}

std::unique_ptr<cursiv::AstReflection> create_workspace_reflection() {
  return ws::generated::Workspace::create_ast_reflection();
}

}  // namespace support
