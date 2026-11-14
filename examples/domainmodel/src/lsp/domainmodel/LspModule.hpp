#pragma once

#include <memory>
#include <string>

#include "cursiv/services/LspLanguage.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "domainmodel/LspServices.hpp"
#include "domainmodel/generated/DomainModel/Module.hpp"

namespace dm {

// The LSP overrides, on top of the core module's.
void install_lsp_module(DomainModelServices& services);

// The shared services and the language, created in one call.
using DomainModelLanguage =
    cursiv::CoreLanguage<DomainModelServices, DomainModelSharedServices>;

DomainModelLanguage create_lsp_services(
    std::unique_ptr<cursiv::FileSystemProvider> files = nullptr);

void register_lsp_services(cursiv::SharedCoreServices& sharedServices);

}  // namespace dm
