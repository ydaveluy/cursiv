#pragma once

#include <memory>
#include <string>

#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "statemachine/generated/SM/Module.hpp"
#include "statemachine/lsp/LspServices.hpp"

namespace sm {

void install_lsp_module(StateMachineServices& services);

std::unique_ptr<StateMachineServices> create_lsp_services(
    StateMachineSharedServices& sharedServices,
    std::string languageId = std::string(sm::generated::SM::kLanguageId));

// The same one call, one storey up.
struct StateMachineLanguage {
  std::unique_ptr<StateMachineSharedServices> shared;
  StateMachineServices& language;
};

StateMachineLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace sm
