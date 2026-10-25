// A nonterminal's name, for the dumps and the diagnostics.
#pragma once

#include <cstdint>

namespace cursiv {

struct LrNtInfo {
  std::uint16_t nameOff = 0, nameLen = 0;

  constexpr bool operator==(const LrNtInfo&) const = default;
};

}  // namespace cursiv
