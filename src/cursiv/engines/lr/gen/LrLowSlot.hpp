// One position of one right-hand side, in the pool the productions index.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LrLowSlot {
  // The actions the call site adds to the record this position produced.
  std::uint16_t row = kNoRow;

  constexpr bool operator==(const LrLowSlot&) const = default;
};

}  // namespace cursiv
