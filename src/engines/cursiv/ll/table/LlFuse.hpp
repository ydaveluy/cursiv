// One join a call site owes: the chain its call handed back, and that chain
// followed by what the site states over it.
#pragma once

#include <cstdint>

namespace cursiv {

struct LlFuse {
  std::uint16_t from = 0;
  std::uint16_t to = 0;

  constexpr bool operator==(const LlFuse&) const = default;
};

}  // namespace cursiv
