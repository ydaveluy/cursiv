#pragma once

#include <memory>
#include <string>

#include "cursiv/services/LspLanguage.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "statemachine/LspServices.hpp"
#include "statemachine/generated/SM/Module.hpp"

namespace sm {

void install_lsp_module(StateMachineServices& services);

// The shared services and the language, created in one call.
using StateMachineLanguage =
    cursiv::CoreLanguage<StateMachineServices, StateMachineSharedServices>;

StateMachineLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

void register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace sm
