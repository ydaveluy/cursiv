#pragma once

#include <memory>
#include <string>

#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"
#include "domainmodel/lsp/LspServices.hpp"

namespace dm {

// This storey's overrides, on top of the core module's.
void install_lsp_module(DomainModelServices& services);

std::unique_ptr<DomainModelServices> create_lsp_services(
    DomainModelSharedServices& sharedServices,
    std::string languageId =
        std::string(dm::generated::DomainModel::kLanguageId));

// The same one call, one storey up.
struct DomainModelLanguage {
  std::unique_ptr<DomainModelSharedServices> shared;
  DomainModelServices& language;
};

DomainModelLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

bool register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace dm
