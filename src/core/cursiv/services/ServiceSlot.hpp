#pragma once

#include <string_view>

// One service slot of a container. A container lists its own slots, and the
// completeness check reads only that list, so a new service is declared in one
// place.

namespace cursiv {

// `required`: the language cannot work without the slot. The registry refuses a
// container missing one, and names it.
struct ServiceSlot {
  std::string_view name;
  bool required = false;
  bool installed = false;
};

namespace detail {

// The first required slot nothing has filled, empty when the container is
// complete. A derived container adds its own slots.
template <class Services>
std::string_view first_missing_service(const Services& services) {
  std::string_view missing;
  services.visitServices([&missing](const ServiceSlot& slot) {
    if (missing.empty() && slot.required && !slot.installed)
      missing = slot.name;
  });
  return missing;
}

}  // namespace detail

}  // namespace cursiv
