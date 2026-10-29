#pragma once

#include <algorithm>
#include <cstdint>

#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/ll/runtime/ll_repair.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"

namespace cursiv {

template <const auto& T>
inline void ll_condition_node(LlContext& c, std::uint16_t mode,
                              std::uint16_t condition, std::uint16_t node) {
  if (c.give) return;
  for (;;) {
    ll_peek<T>(c, mode);
    if (ll_condition<T>(c, condition, c.la, c.at)) {
      ll_hold<T>(c);
      return;
    }
    // A guard refuses a reading just as a token does. Its deletion and the
    // caller's abandon belong to the same election, with the same costs.
    ll_repair_peek<T>(c);
    const LlPause pause = ll_pause<T>(c, LlRoot{kNoRow, node});
    if (const LlGesture* g = pause.gesture;
        g != nullptr && g->kind != LlGesture::ins) {
      ll_write<T>(c, mode, *g);
      if (c.give) return;
      continue;
    }
    ll_unfinished<T>(c);
    c.give = true;
    return;
  }
}

template <const auto& T>
inline void ll_implied(LlContext& c, std::uint16_t mode, std::uint16_t term,
                       std::uint16_t condition, std::uint16_t row,
                       std::uint16_t pos = 0) {
  if (c.give) return;
  if (!c.laValid) ll_peek<T>(c, mode);
  if (c.la.sym == term) {
    ll_consume<T>(c, row, pos);
  } else if (ll_condition<T>(c, condition, ll_normal_token<T>(c, c.la, mode),
                             c.at)) {
    // It stands before the gap, as an explicit separator would. A parent
    // may already have flushed hidden trivia, so keep the tape monotone.
    const CstRun& run = c.tape.stream();
    const TextOffset floor =
        run.records() != 0 ? run.at(run.records() - 1u).end : 0;
    const TextOffset at = std::max(c.at, floor);
    c.last = c.tape.leaf(at, at, T.stamp.make(row, pos));
  } else {
    ll_expect<T>(c, mode, term, row, pos);
  }
}

}  // namespace cursiv
