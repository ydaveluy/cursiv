#pragma once

#include <memory>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/CoreServicesFor.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "workspace/core/references/QualifiedNameProvider.hpp"
#include "workspace/core/validation/WorkspaceValidator.hpp"

namespace ws {

// Empty, so the language's services derive from a shared type of their own.
struct WorkspaceSharedCoreServices : virtual cursiv::SharedCoreServices {};

struct WorkspaceCoreServices
    : cursiv::CoreServicesFor<WorkspaceSharedCoreServices> {
  // The qualifier a declaration answers to from outside its package.
  std::unique_ptr<const QualifiedNameProvider> qualifiedNameProvider;
  // Owns the checks; the validation registry holds pointers into it.
  std::unique_ptr<const WorkspaceValidator> validator;

  using CoreServicesFor::CoreServicesFor;

  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    cursiv::CoreServices::visitServices(yield);
    yield({"qualifiedNameProvider", true, qualifiedNameProvider != nullptr});
    yield({"validator", true, validator != nullptr});
  }
};

void install_core_module(WorkspaceCoreServices& services);

}  // namespace ws
