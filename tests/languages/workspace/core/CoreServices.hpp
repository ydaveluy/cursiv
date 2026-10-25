#pragma once

#include <memory>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/CoreServicesFor.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "workspace/core/references/QualifiedNameProvider.hpp"
#include "workspace/core/validation/WorkspaceValidator.hpp"

// What this language owns beyond what every language owns, and the module
// that fills its container.

namespace ws {

// Nothing workspace-wide yet; it exists so the language's container is built
// from its own shared type.
struct WorkspaceSharedCoreServices : virtual cursiv::SharedCoreServices {};

struct WorkspaceCoreServices
    : cursiv::CoreServicesFor<WorkspaceSharedCoreServices> {
  // The qualifier a declaration answers to from outside its package.
  std::unique_ptr<const QualifiedNameProvider> qualifiedNameProvider;
  // The class whose methods are this language's checks. Defined once here;
  // the registry files pointers into it, never copies.
  std::unique_ptr<const WorkspaceValidator> validator;

  explicit WorkspaceCoreServices(
      WorkspaceSharedCoreServices& sharedServices)
      : cursiv::CoreServices(sharedServices), CoreServicesFor(sharedServices) {}

  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"qualifiedNameProvider", true, qualifiedNameProvider != nullptr});
    yield({"validator", true, validator != nullptr});
  }
};

void install_core_module(WorkspaceCoreServices& services);

}  // namespace ws
