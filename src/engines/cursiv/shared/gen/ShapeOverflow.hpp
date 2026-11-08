// The first shape limit a construction ran into, named for what it bounds.
#pragma once

namespace cursiv::engine {

struct ShapeOverflow {
  bool overflow = false;
  const char* overflowCap = nullptr;

  constexpr void cap_hit(const char* cap) {
    overflow = true;
    if (!overflowCap) overflowCap = cap;
  }
};

}  // namespace cursiv::engine
