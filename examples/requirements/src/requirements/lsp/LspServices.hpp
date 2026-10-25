#pragma once

#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "requirements/core/CoreServices.hpp"

namespace req {

struct RequirementsSharedServices : RequirementsSharedCoreServices,
                                    cursiv::SharedLspServices {};

struct RequirementsServices final : RequirementsCoreServices,
                                    cursiv::LspServices {
  explicit RequirementsServices(RequirementsSharedServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        RequirementsCoreServices(sharedServices),
        cursiv::LspServices(sharedServices) {}

  // One emission per storey through the diamond: the language rows through
  // the core side, the LSP rows alone.
  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    RequirementsCoreServices::visitServices(yield);
    cursiv::LspServices::visitOwnServices(yield);
  }
};

}  // namespace req
