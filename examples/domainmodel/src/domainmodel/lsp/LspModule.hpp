#pragma once

#include <memory>
#include <string>

#include <domainmodel/lsp/LspServices.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace domainmodel {

/// LSP service overrides applied on top of pegium's default LSP services.
void installDomainModelLspModule(DomainModelServices &services);

/// Builds the LSP-enabled domain-model language services.
std::unique_ptr<DomainModelServices>
createDomainModelLspServices(const DomainModelSharedServices &sharedServices,
                          std::string languageId = "domain-model");

/// Registers the LSP-enabled domain-model services in `sharedServices`.
bool registerDomainModelLspServices(pegium::SharedServices &sharedServices);

} // namespace domainmodel
