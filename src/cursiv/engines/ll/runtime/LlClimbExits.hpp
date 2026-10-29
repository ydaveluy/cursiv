// Where a climb may end, at its exact call site: two calls made by the same
// rule need not have the same followers. Each frame the descent stands in
// names what its rule reads once the call returns, and whether that may be
// nothing: the frames are read from the nearest down to the first that must
// read something, so a deep descent costs only the frames that may be empty.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"

namespace cursiv {

// Each row the climb's end may return through, the nearest frame first, until
// `read` answers true or a frame must read something. True where `read` did,
// or where the frames ran out with no caller known: any token may follow.
template <const auto& T, class Read>
inline bool ll_climb_rows(const LlContext& c, Read&& read) {
  static_assert(T.climbExitAt != nullptr);
  // The document reads its end once the entry returns.
  constexpr std::uint32_t document =
      T.atn.edge(T.atn.state(T.atnStart).edge).follow;
  for (std::size_t i = c.stack.size() + 1; i-- > 0;) {
    if (i == 0 && !c.document) return true;
    const std::uint32_t at = T.climbExitAt[i != 0 ? c.stack[i - 1] : document];
    if (read(LlTermRow{T.climbExits + (at >> 1), T.termWords})) return true;
    if ((at & 1u) == 0) return false;
  }
  return false;
}

// Whether another caller's longer follower covers this caller's closer: the
// token in hand is read again under every follower the frames admit.
template <const auto& T>
CURSIV_NOINLINE inline bool ll_climb_reads_on(const LlContext& c) {
  LlMask<T.termWords> viable;
  ll_climb_rows<T>(c, [&](LlTermRow row) {
    for (std::uint32_t k = 0; k < row.words; ++k)
      for (std::uint64_t w = row.w[k]; w != 0; w &= w - 1)
        viable.set(static_cast<std::uint16_t>(k * 64 + std::countr_zero(w)));
    return false;
  });
  thread_local std::vector<std::uint16_t> read;
  read.clear();
  return ll_match_in<T>(c.src, c.len, c.la.begin, viable, read) > c.la.begin;
}

// Whether this caller can continue at the climb's end.
template <const auto& T>
inline bool ll_climb_exits(const LlContext& c, std::uint16_t sym) {
  if (ll_ends<T>(c, sym)) return true;
  if (ll_climb_rows<T>(c, [&](LlTermRow row) { return row.test(sym); }))
    return true;
  return c.laValid && sym == c.la.sym && ll_climb_reads_on<T>(c);
}

}  // namespace cursiv
