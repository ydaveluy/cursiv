#pragma once

#include "arithmetics/CoreServices.hpp"
#include "cursiv/services/LspServicesFor.hpp"
#include "cursiv/services/SharedLspServices.hpp"

// The same language with its LSP services. Both containers derive
// `CoreServices` virtually, so this holds one of it and the core module wires
// the same object the LSP providers read.

namespace arith {

struct ArithmeticsSharedServices : ArithmeticsSharedCoreServices,
                                   cursiv::SharedLspServices {};

struct ArithmeticsServices final
    : cursiv::LspServicesFor<ArithmeticsCoreServices, ArithmeticsSharedServices> {
  using LspServicesFor::LspServicesFor;
};

}  // namespace arith
