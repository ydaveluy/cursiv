#include "arithmetics/LspModule.hpp"

#include <memory>
#include <utility>

#include "arithmetics/CoreServices.hpp"
#include "arithmetics/ArithmeticsCodeActionProvider.hpp"
#include "arithmetics/ArithmeticsFormatter.hpp"
#include "cursiv/semantic/GrammarSemanticTokenProvider.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/services/SharedLspServices.hpp"

namespace arith {

void install_lsp_module(ArithmeticsServices& services) {
  services.semanticTokenProvider =
      std::make_unique<const cursiv::GrammarSemanticTokenProvider>(services);
  services.formatter = std::make_unique<const ArithmeticsFormatter>(services);
  services.codeActionProvider =
      std::make_unique<const ArithmeticsCodeActionProvider>();
}

ArithmeticsLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files) {
  return cursiv::create_lsp_language<ArithmeticsServices,
                                     ArithmeticsSharedServices,
                                     install_core_module, install_lsp_module>(
      std::move(files));
}

void register_lsp_services(cursiv::SharedCoreServices& sharedServices) {
  cursiv::register_lsp_language<ArithmeticsServices, ArithmeticsSharedServices,
                                install_core_module, install_lsp_module>(
      sharedServices);
}

}  // namespace arith
