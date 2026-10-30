// Where a climb may end, at its exact call site: what the frames the descent
// stands in read once the climb returns.
#pragma once

#include <bit>
#include <cstdint>
#include <vector>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_callers.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"

namespace cursiv {

// Whether the token in hand is one another caller takes: read again under
// every follower the frames admit, it is this caller's closer that a longer
// follower covered, or a blank -- a newline only another caller reads as a
// token -- that this caller crosses to the follower behind it.
template <const auto& T>
CURSIV_NOINLINE inline bool ll_climb_reads_on(const LlContext& c) {
  LlMask<T.termWords> viable;
  ll_caller_rows<T>(c, [&](LlTermRow row) {
    for (std::uint32_t k = 0; k < row.words; ++k)
      for (std::uint64_t w = row.w[k]; w != 0; w &= w - 1)
        viable.set(static_cast<std::uint16_t>(k * 64 + std::countr_zero(w)));
    return false;
  });
  thread_local std::vector<std::uint16_t> read;
  TextOffset p = c.la.begin;
  if (c.la.end > c.la.begin && c.la.mode < T.nmodes) {
    const bool joined = kIndented<T> && c.indent.depth != 0;
    for (;;) {
      TextOffset inspected = p;
      bool ended = false;
      const Scan blank = ll_look_blank<T>(c, p, c.la.mode, joined, read,
                                          inspected, ended, &viable);
      if (blank.term == kInvalidSymbol || blank.end == p) break;
      p = blank.end;
    }
    if (p >= c.len) return viable.test(kEofSymbol);
  }
  read.clear();
  return ll_match_in<T>(c.src, c.len, p, viable, read) > p;
}

// Whether this caller can continue at the climb's end.
template <const auto& T>
inline bool ll_climb_exits(const LlContext& c, std::uint16_t sym) {
  if (ll_callers_take<T>(c, sym)) return true;
  return c.laValid && sym == c.la.sym && ll_climb_reads_on<T>(c);
}

}  // namespace cursiv
