#pragma once

#include "arithmetics/core/CoreServices.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"

// The same language one storey up. Both containers derive `CoreServices`
// virtually, so this holds one of it and the core module wires the same object
// the LSP providers read.

namespace arith {

struct ArithmeticsSharedServices : ArithmeticsSharedCoreServices,
                                   cursiv::SharedLspServices {};

struct ArithmeticsServices final : ArithmeticsCoreServices,
                                   cursiv::LspServices {
  explicit ArithmeticsServices(ArithmeticsSharedServices& sharedServices)
      : cursiv::CoreServices(sharedServices),
        ArithmeticsCoreServices(sharedServices),
        cursiv::LspServices(sharedServices) {}

  // One emission per storey through the diamond: the language rows through
  // the core side, the LSP rows alone.
  void visitServices(cursiv::function_ref<void(const cursiv::ServiceSlot&)>
                         yield) const override {
    ArithmeticsCoreServices::visitServices(yield);
    cursiv::LspServices::visitOwnServices(yield);
  }
};

}  // namespace arith
