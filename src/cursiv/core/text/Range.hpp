#pragma once

#include "cursiv/core/text/Position.hpp"

// A range is a span an editor understands: the same extent a byte range
// names, said in the client's unit.

namespace cursiv {

// Half-open, like every published range.
struct Range {
  Position start;
  Position end;

  [[nodiscard]] constexpr bool operator==(const Range&) const noexcept =
      default;
};

}  // namespace cursiv
