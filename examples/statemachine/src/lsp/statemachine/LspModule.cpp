#include "statemachine/LspModule.hpp"

#include <memory>
#include <utility>

#include "cursiv/semantic/GrammarSemanticTokenProvider.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "statemachine/CoreServices.hpp"
#include "statemachine/StateMachineFormatter.hpp"

namespace sm {

void install_lsp_module(StateMachineServices& services) {
  services.semanticTokenProvider =
      std::make_unique<const cursiv::GrammarSemanticTokenProvider>(services);
  services.formatter = std::make_unique<const StatemachineFormatter>(services);
}

StateMachineLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files) {
  return cursiv::create_lsp_language<StateMachineServices,
                                     StateMachineSharedServices,
                                     install_core_module, install_lsp_module>(
      std::move(files));
}

void register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  cursiv::register_lsp_language<StateMachineServices,
                                StateMachineSharedServices, install_core_module,
                                install_lsp_module>(sharedServices);
}

}  // namespace sm
