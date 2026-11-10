// What one table entry does with the token in hand.
#pragma once

#include <cstdint>

namespace cursiv::engine {

// A `guard` is a shift whose target the gap before the token picks: its
// argument names the program that says which.
enum class LrActionKind : std::uint8_t { shift, reduce, accept, guard };

constexpr bool lr_takes_token(LrActionKind k) {
  return k == LrActionKind::shift || k == LrActionKind::guard;
}

}  // namespace cursiv::engine
