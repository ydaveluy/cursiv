#pragma once

#include <memory>
#include <string>

#include "cursiv/workspace/FileSystemProvider.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "domainmodel/LspServices.hpp"

namespace dm {

// The LSP overrides, on top of the core module's.
void install_lsp_module(DomainModelServices& services);

std::unique_ptr<DomainModelServices> create_lsp_services(
    DomainModelSharedServices& sharedServices,
    std::string languageId =
        std::string(dm::generated::DomainModel::kLanguageId));

// The shared services and the language, created in one call.
struct DomainModelLanguage {
  std::unique_ptr<DomainModelSharedServices> shared;
  DomainModelServices& language;
};

DomainModelLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace dm
