// One position a reduce names: the frame's slot and the row it takes.
#pragma once

#include <compare>
#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"

namespace cursiv {

struct LrReduceName {
  std::uint16_t slot = 0;
  ElementId row = kNoElement;
  bool fold = false;  // compose the returned row with the site

  auto operator<=>(const LrReduceName&) const = default;
};

}  // namespace cursiv
