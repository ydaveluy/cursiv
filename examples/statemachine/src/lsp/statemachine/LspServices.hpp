#pragma once

#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "statemachine/CoreServices.hpp"

// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same
// object the LSP providers read.

namespace sm {

struct StateMachineSharedServices : StateMachineSharedCoreServices,
                                    cursiv::SharedLspServices {};

struct StateMachineServices final
    : cursiv::LspServicesFor<StateMachineCoreServices, StateMachineSharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace sm
