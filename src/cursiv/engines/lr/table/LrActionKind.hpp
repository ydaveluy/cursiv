// What one table entry does with the token in hand.
#pragma once

#include <cstdint>

namespace cursiv {

// A `guard` is a shift the gap before the token lands: its argument names the
// program that says where.
enum class LrActionKind : std::uint8_t { shift, reduce, accept, guard };

constexpr bool lr_takes_token(LrActionKind k) {
  return k == LrActionKind::shift || k == LrActionKind::guard;
}

}  // namespace cursiv
