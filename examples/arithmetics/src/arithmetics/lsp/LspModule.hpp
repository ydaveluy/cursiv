#pragma once

#include <memory>
#include <string>

#include "arithmetics/generated/Arith/Module.hpp"
#include "arithmetics/lsp/LspServices.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"

namespace arith {

void install_lsp_module(ArithmeticsServices& services);

std::unique_ptr<ArithmeticsServices> create_lsp_services(
    ArithmeticsSharedServices& sharedServices,
    std::string languageId = std::string(arith::generated::Arith::kLanguageId));

// The same one call, one storey up.
struct ArithmeticsLanguage {
  std::unique_ptr<ArithmeticsSharedServices> shared;
  ArithmeticsServices& language;
};

ArithmeticsLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace arith
