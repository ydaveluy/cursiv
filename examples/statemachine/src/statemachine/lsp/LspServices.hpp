#pragma once

#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "statemachine/core/CoreServices.hpp"

// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same
// object the LSP providers read.

namespace sm {

struct StateMachineSharedServices : StateMachineSharedCoreServices,
                                    cursiv::SharedLspServices {};

struct StateMachineServices final : StateMachineCoreServices,
                                    cursiv::LspServices {
  explicit StateMachineServices(StateMachineSharedServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        StateMachineCoreServices(sharedServices),
        cursiv::LspServices(sharedServices) {}

  // One emission per storey through the diamond: the language rows through
  // the core side, the LSP rows alone.
  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    StateMachineCoreServices::visitServices(yield);
    cursiv::LspServices::visitOwnServices(yield);
  }
};

}  // namespace sm
