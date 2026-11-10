#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"

// A language's own container, typed: retypes `shared` and does nothing else,
// so a language whose workspace carries custom shared services reaches them
// without a cast.

namespace cursiv {

// Derives virtually, so a language that is both core and LSP holds one
// `CoreServices`. A language derives it, adds its slots and inherits the
// constructor (`using CoreServicesFor::CoreServicesFor;`).
template <class TShared>
struct CoreServicesFor : virtual CoreServices {
  static_assert(std::is_base_of_v<SharedCoreServices, TShared>,
                "TShared derives from cursiv::SharedCoreServices");

  // The same object as the base's `shared()`, as the type the language gave it.
  TShared& shared;

  explicit CoreServicesFor(TShared& sharedServices) : shared(sharedServices) {
    bindShared(sharedServices);
  }
};

// One language container with the defaults filled, before a module overrides
// them. `TShared` is deduced, so a custom workspace type reaches the
// container's constructor as itself.
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
