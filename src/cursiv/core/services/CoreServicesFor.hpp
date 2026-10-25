#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"

// A language's own container, typed: retypes `shared` and does nothing else,
// so a language whose workspace carries custom shared services reaches them
// without a cast.

namespace cursiv {

// Derives virtually, so a language that is both a core language and an LSP one
// holds exactly one `CoreServices` through the diamond. A language derives this
// and adds its own slots; it must still spell the constructor out, an inherited
// one never initialising a virtual base:
//
//   struct MyCoreServices : cursiv::CoreServicesFor<MySharedCoreServices> {
//     std::unique_ptr<const MyValidator> validator;
//     explicit MyCoreServices(MySharedCoreServices& sharedServices)
//         : cursiv::CoreServices(sharedServices),
//         CoreServicesFor(sharedServices)
//         {}
//   };
template <class TShared>
struct CoreServicesFor : virtual CoreServices {
  static_assert(std::is_base_of_v<SharedCoreServices, TShared>,
                "TShared derives from cursiv::SharedCoreServices");

  // Shadows `CoreServices::shared`, which names the same object.
  TShared& shared;

  explicit CoreServicesFor(TShared& sharedServices)
      : CoreServices(sharedServices), shared(sharedServices) {}
};

// One language container with the defaults filled -- the headless counterpart
// of what a module then overrides. `TShared` is deduced, so a custom workspace
// type reaches the container's constructor as itself.
template <class TServices = CoreServices, class TShared>
[[nodiscard]] std::unique_ptr<TServices> make_default_core_services(
    TShared& sharedServices, std::string languageId) {
  static_assert(std::is_base_of_v<CoreServices, TServices>,
                "TServices derives from cursiv::CoreServices");
  static_assert(std::is_constructible_v<TServices, TShared&>,
                "TServices is constructible from TShared&");

  auto services = std::make_unique<TServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  install_default_core_services(*services);
  return services;
}

}  // namespace cursiv
