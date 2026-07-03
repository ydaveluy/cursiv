#pragma once

#include <memory>
#include <string>

#include <domainmodel/core/CoreServices.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>

namespace domainmodel {

/// Creates a standalone domain-model parser.
std::unique_ptr<const pegium::parser::Parser> createDomainModelParser();

/// Wires the domain-model core overrides onto a service container.
///
/// Takes the pegium core base and the domain-model graft as two separate
/// references, so it is a plain function rather than a template: every container
/// is-a `pegium::CoreServices` and is-a `DomainModelAddedServices`, so the
/// headless and the LSP bundle both wire themselves with
/// `installDomainModelCoreModule(*services, *services)`.
void installDomainModelCoreModule(pegium::CoreServices &core,
                                  DomainModelAddedServices &added);

/// Builds the core-only domain-model language services.
std::unique_ptr<DomainModelCoreServices>
createDomainModelCoreServices(const pegium::SharedCoreServices &sharedServices,
                              std::string languageId = "domain-model");

/// Registers the core-only domain-model services in `sharedServices`.
bool registerDomainModelCoreServices(pegium::SharedCoreServices &sharedServices);

} // namespace domainmodel
