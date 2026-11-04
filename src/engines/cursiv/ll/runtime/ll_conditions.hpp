#pragma once

#include <cstdint>

#include "cursiv/utils/macros.hpp"
#include "cursiv/ll/runtime/ll_read.hpp"
#include "cursiv/ll/runtime/ll_repair.hpp"
#include "cursiv/ll/runtime/ll_section.hpp"

namespace cursiv {

template <const auto& T>
CURSIV_NOINLINE inline void ll_condition_node(LlContext& c, std::uint16_t mode,
                                              std::uint16_t condition,
                                              std::uint16_t node) {
  if (c.give) return;
  for (;;) {
    ll_peek<T>(c, mode);
    bool holds = ll_condition<T>(c, condition, c.la, c.at);
    // A terminal it refuses comes off the position, which is read again; a
    // token only a condition names stands, and nothing here reads it. Where no
    // reading holds, the token first read is the one that is repaired.
    if (!holds) {
      const Token first = c.la;
      const std::uint16_t firstMode = c.laMode;
      LlSkipped<T> skip{};
      while (!holds && !ll_lengthens<T>(mode, c.la.sym) &&
             ll_read_without<T>(c, mode, skip))
        holds = !ll_lengthens<T>(mode, c.la.sym) &&
                ll_condition<T>(c, condition, c.la, c.at);
      if (!holds) {
        c.la = first;
        c.laMode = firstMode;
      }
    }
    if (holds) {
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
  if (c.la.sym == term)
    ll_consume<T>(c, row, pos);
  else if (!ll_implied_holds<T>(c, mode, condition, row, pos))
    ll_expect<T>(c, mode, term, row, pos, condition);
}

}  // namespace cursiv
