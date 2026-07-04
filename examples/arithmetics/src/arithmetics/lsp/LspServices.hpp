#pragma once

#include <arithmetics/core/CoreServices.hpp>
#include <pegium/lsp/services/Services.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace arithmetics {

/// Workspace-wide shared services in an LSP session.
struct ArithmeticsSharedServices : ArithmeticsSharedCoreServices,
                                   pegium::SharedServices {};

/// LSP-enabled arithmetics language services.
struct ArithmeticsServices final
    : pegium::ServicesFor<ArithmeticsCoreServices, ArithmeticsSharedServices> {
  explicit ArithmeticsServices(const ArithmeticsSharedServices &sharedServices)
      : pegium::CoreServices(sharedServices), ServicesFor(sharedServices) {}
};

} // namespace arithmetics
