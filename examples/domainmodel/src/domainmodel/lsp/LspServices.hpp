#pragma once

#include <domainmodel/core/CoreServices.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace domainmodel {

/// Workspace-wide shared services in an LSP session.
struct DomainModelSharedServices : DomainModelSharedCoreServices,
                                   pegium::SharedServices {};

/// LSP-enabled domain-model language services.
struct DomainModelServices final
    : pegium::ServicesFor<DomainModelCoreServices, DomainModelSharedServices> {
  explicit DomainModelServices(const DomainModelSharedServices &sharedServices)
      : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
};

} // namespace domainmodel
