#pragma once

#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "domainmodel/core/CoreServices.hpp"

// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same
// object the LSP providers read.

namespace dm {

struct DomainModelSharedServices : DomainModelSharedCoreServices,
                                   cursiv::SharedLspServices {};

struct DomainModelServices final : DomainModelCoreServices,
                                   cursiv::LspServices {
  explicit DomainModelServices(DomainModelSharedServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        DomainModelCoreServices(sharedServices),
        cursiv::LspServices(sharedServices) {}

  // One emission per storey through the diamond: the language rows through
  // the core side, the LSP rows alone.
  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    DomainModelCoreServices::visitServices(yield);
    cursiv::LspServices::visitOwnServices(yield);
  }
};

}  // namespace dm
