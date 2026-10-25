// What the gap decides, read where the parse stands: a guard's landing by the
// outcome of its conditions, and what a state's tag says of the gap its span
// opened on.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cursiv/engines/lr/table/LrActionKind.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// The span of an empty reduction opened on no state: its goto asks the gap.
inline constexpr std::uint32_t kLrNoFirst = 0xFFFFFFFFu;

// Whether the gap decides any transition of table `A`; a table that names no
// guard at all decides none.
template <const auto& A>
inline constexpr bool kLrGuarded = A.guardOps.size() != 0;

// Whether action kind `k` takes the token: one comparison without a guard.
template <const auto& A>
constexpr bool lr_takes_token_in(LrActionKind k) {
  return k == LrActionKind::shift ||
         (kLrGuarded<A> && k == LrActionKind::guard);
}

// Whether condition `k` held on the gap the span of state `s` opened on: a
// state whose tag says nothing of it held it by its own items.
template <const auto& A>
constexpr bool lr_held(std::uint32_t s, unsigned k) {
  constexpr std::size_t kWords = (A.mode.size() + 63u) / 64u;
  return ((A.held[k * kWords + (s >> 6)] >> (s & 63u)) & 1u) != 0;
}

// Where guard `k` lands, `holds(c)` answering each of its conditions;
// `kInvalidSymbol` where the outcome lands nowhere.
template <const auto& A, class Holds>
constexpr std::uint32_t lr_guard_land(std::uint32_t k, Holds&& holds) {
  const std::uint32_t at = A.guardAt[k];
  const unsigned n = A.guardOps[at];
  unsigned v = 0;
  for (unsigned b = 0; b < n; ++b)
    if (holds(A.guardOps[at + 1u + b])) v |= 1u << b;
  const std::uint16_t to = A.guardOps[at + 1u + n + v];
  return to == 0xffffu ? kInvalidSymbol : to;
}

// The goto on a column cell: a guard above the states lands by the tag of
// `first`, or by the gap where the span opened on none.
template <const auto& A, class Holds>
constexpr std::uint32_t lr_goto_land(std::uint32_t cell, std::uint32_t first,
                                     Holds&& holds) {
  if constexpr (!kLrGuarded<A>) {
    (void)first, (void)holds;
    return cell;
  } else {
    constexpr std::uint32_t kStates = A.mode.size();
    if (cell == kInvalidSymbol || cell < kStates) return cell;
    if (first == kLrNoFirst) return lr_guard_land<A>(cell - kStates, holds);
    return lr_guard_land<A>(cell - kStates,
                            [&](unsigned k) { return lr_held<A>(first, k); });
  }
}

}  // namespace cursiv
