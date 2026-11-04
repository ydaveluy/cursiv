#pragma once

#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "domainmodel/CoreServices.hpp"

// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same
// object the LSP providers read.

namespace dm {

struct DomainModelSharedServices : DomainModelSharedCoreServices,
                                   cursiv::SharedLspServices {};

struct DomainModelServices final
    : cursiv::LspServicesFor<DomainModelCoreServices, DomainModelSharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace dm
