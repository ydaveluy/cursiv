#pragma once

#include <memory>
#include <string>

#include <domainmodel/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace domainmodel {

/// Creates a standalone domain-model parser.
std::unique_ptr<const pegium::parser::Parser> createDomainModelParser();

/// Wires the domain-model core overrides. The LSP path reuses it directly.
void installDomainModelCoreModule(DomainModelCoreServices &services);

/// Builds the core-only domain-model language services.
std::unique_ptr<DomainModelCoreServices>
createDomainModelCoreServices(
    const DomainModelSharedCoreServices &sharedServices,
    std::string languageId = "domain-model");

/// Registers the core-only domain-model services in `sharedServices`.
bool registerDomainModelCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace domainmodel
