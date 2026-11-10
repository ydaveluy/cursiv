// A language's LSP container: its core services and the editor services, over
// one `CoreServices`.
#pragma once

#include <type_traits>

#include "cursiv/services/LspServices.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/utils/FunctionRef.hpp"

namespace cursiv {

// A language derives this and inherits its constructor
// (`using LspServicesFor::LspServicesFor;`).
template <class TCore, class TShared>
struct LspServicesFor : TCore, LspServices {
  static_assert(std::is_base_of_v<CoreServices, TCore>,
                "TCore derives from cursiv::CoreServices");
  static_assert(std::is_base_of_v<SharedLspServices, TShared>,
                "TShared derives from cursiv::SharedLspServices");

  explicit LspServicesFor(TShared& sharedServices)
      : TCore(sharedServices), LspServices(sharedServices) {}

  // Each set once through the diamond: the language rows through the core
  // side, then the LSP rows alone.
  void visitServices(
      function_ref<void(const ServiceSlot&)> yield) const override {
    TCore::visitServices(yield);
    LspServices::visitOwnServices(yield);
  }
};

}  // namespace cursiv
