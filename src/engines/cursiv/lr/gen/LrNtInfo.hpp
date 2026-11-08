// A nonterminal's name, for the dumps and the diagnostics.
#pragma once

#include <cstdint>

namespace cursiv::engine {

struct LrNtInfo {
  std::uint16_t nameOff = 0, nameLen = 0;
  // A rule that builds a node, as neither a data rule nor an auxiliary does.
  bool node = false;

  constexpr bool operator==(const LrNtInfo&) const = default;
};

}  // namespace cursiv::engine
