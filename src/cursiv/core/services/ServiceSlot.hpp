#pragma once

#include <string_view>

// What a language owes, stated once. A container yields its own slots; the
// completeness question reads what it yields and nothing else, so a service
// added declares itself in one place.

namespace cursiv {

// `required` says a language cannot answer without the slot. An absent slot is
// reported by name, never filled with something that lies: the registry refuses
// a container that owes one.
struct ServiceSlot {
  std::string_view name;
  bool required = false;
  bool installed = false;
};

// The first required slot nothing has filled, empty when the container is
// complete. A storey above appends its own rows and this stays the one answer.
template <class Services>
std::string_view first_missing_service(const Services& services) {
  std::string_view missing;
  services.visitServices([&missing](const ServiceSlot& slot) {
    if (missing.empty() && slot.required && !slot.installed)
      missing = slot.name;
  });
  return missing;
}

}  // namespace cursiv
