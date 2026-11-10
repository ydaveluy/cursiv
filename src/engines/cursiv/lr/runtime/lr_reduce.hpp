// The reduction shared by generated states and repair replay: production and
// target are constants on the nominal path, table values in the writer.
#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrFrame.hpp"
#include "cursiv/lr/runtime/lr_moves.hpp"
#include "cursiv/lr/runtime/lr_states.hpp"
#include "cursiv/lr/table/LrReduceForm.hpp"
#include "cursiv/lr/table/LrReducePlan.hpp"
#include "cursiv/shared/table/StampLayout.hpp"

namespace cursiv::engine {

template <const auto& A>
inline constexpr bool kLrPrefixFrames = [] {
  for (const auto& plan : A.derived.reductions)
    if (plan.turnRow != kNoElement) return true;
  return false;
}();

// A turn's receiver, the records `[from, to)` hold: none where they hold
// trivia alone, one record over them where they hold several.
template <StampLayout L>
inline bool lr_receiver(LrContext& c, NodeCount from, NodeCount to,
                        TextOffset begin) {
  NodeCount last = to;
  unsigned seen = 0;
  for (NodeCount x = to; x > from && seen < 2;
       x -= c.tape.at(x - 1).subtreeSize)
    if (!c.tape.at(x - 1).hidden && seen++ == 0) last = x - 1;
  if (seen > 1) c.tape.group<L>(from, last, begin);
  return seen != 0;
}

// Prepares the result frame, keeping its first state's tag for a guarded goto,
// and returns a record the target must close. An integral-constant production
// unrolls its renames.
template <const auto& A, class Prod, class Folds>
CURSIV_ALWAYS_INLINE inline ElementId lr_reduce_frame(LrContext& c, Prod prod,
                                                      Folds&& folds) {
  const auto& p = A.cfg.prods[prod];
  const auto& plan = A.derived.reductions[prod];
  if (plan.form == LrReduceForm::through) return kNoElement;
  assert(p.len < c.stack.size());
  const std::uint32_t base = c.stack.size() - p.len;
  if (plan.flushes) lr_flush_trivia<A>(c);
  const auto name = [&](const auto& n) CURSIV_ALWAYS_INLINE {
    LrFrame& f = c.stack[base + n.slot];
    if (n.fold)
      lr_name_row<A.stamp>(c, f, n.row, folds);
    else
      lr_name_row_at<A.stamp>(c, f, n.row);
  };
  if constexpr (std::is_integral_v<Prod>) {
    for (unsigned k = 0; k < plan.count; ++k)
      name(A.derived.reduceNames[plan.names + k]);
  } else {
    constexpr auto& fixed = A.derived.reductions[Prod::value];
    [&]<unsigned... K>(std::integer_sequence<unsigned, K...>)
        CURSIV_ALWAYS_INLINE {
      (name(A.derived.reduceNames[fixed.names + K]), ...);
    }(std::make_integer_sequence<unsigned, fixed.count>{});
  }
  const std::uint32_t pendWord =
      plan.carries() ? c.stack[base].word : kLrNoWord;
  const bool last =
      pendWord != kLrNoWord && (pendWord + 1 == c.tape.len() ||
                                lr_last_real_word<A.stamp>(c, pendWord));
  const ElementId held = last ? c.stack[base].row : kNoElement;
  // A turn's record reaches back over what the frame below it created. Grouping
  // that slips a record in, so it waits for the renames above.
  bool reach = false;
  std::uint32_t receiver = base - 1;
  if (plan.turnRow != kNoElement) {
    const NodeCount end = p.len == 0 ? c.tape.len() : c.stack[base].open;
    // Cross only transparent, zero-width guard frames. A row/word, a CST
    // record, or any text is a construction boundary, even if no receiver was
    // found there. Never search into an outer context past such a boundary.
    for (;;) {
      if (lr_receiver<A.stamp>(c, c.stack[receiver].open, end,
                               c.stack[receiver].begin)) {
        reach = true;
        break;
      }
      const auto& frame = c.stack[receiver];
      if (p.len != 0 || receiver == 0 || frame.open != end ||
          frame.begin != c.committed || frame.row != kNoElement ||
          frame.word != kLrNoWord)
        break;
      --receiver;
    }
  }
  ElementId pendRow = plan.form == LrReduceForm::carry ? held : kNoElement;
  ElementId pendNode = reach           ? plan.turnRow
                       : plan.closes() ? p.node
                                       : kNoElement;
  // A record naming no row takes the node's own: two records over one span
  // would be a node nobody wrote.
  if (plan.form == LrReduceForm::foldClose && last && !reach) {
    const ElementId one = held == kNoElement ? p.node : folds(held, p.node);
    if (one != kNoElement) {
      c.tape.rename<A.stamp>(pendWord, one);
      pendNode = kNoElement;
      pendRow = one;
    }
  }
  if (pendNode != kNoElement) pendRow = pendNode;
  std::uint32_t prefix = base;
  if constexpr (kLrPrefixFrames<A>) {
    // A checkpoint after an epsilon prefix may reach before slot zero; its
    // start is kept through the enclosing reduction. Only popped frames are
    // visited.
    for (std::uint32_t i = base + 1; i < c.stack.size(); ++i)
      if (c.stack[i].open < c.stack[prefix].open) prefix = i;
  }
  const bool widened = prefix != base;
  const NodeCount prefixOpen = widened ? c.stack[prefix].open : 0;
  const TextOffset prefixBegin = widened ? c.stack[prefix].begin : 0;
  c.stack.reduce(p.len);
  // Equal opens keep slot zero's original begin. In particular, empty
  // constructs/trivia sharing one tape position do not move its anchor.
  LrFrame& top = c.stack.top();
  if (reach) {
    top.open = c.stack[receiver].open;
    top.begin = c.stack[receiver].begin;
  } else if (p.len == 0) {
    top.open = c.tape.len();
    top.begin = c.committed;
  } else if (widened) {
    top.open = prefixOpen;
    top.begin = prefixBegin;
  }
  top.row = pendRow;
  top.word = last ? pendWord : kLrNoWord;
  assert((plan.row(p.len) == LrReduceRow::none) == (pendNode == kNoElement) ||
         plan.row(p.len) == LrReduceRow::any);
  return pendNode;
}

// `endsCascade`: the target state ends the reduction cascade.
template <StampLayout L>
CURSIV_ALWAYS_INLINE inline void lr_reduce_finish(LrContext& c,
                                                  std::uint32_t to,
                                                  ElementId row,
                                                  bool endsCascade) {
  LrFrame& top = c.stack.top();
  top.state = static_cast<std::uint16_t>(to);
  if (row != kNoElement) {
    top.word = c.tape.len();
    c.tape.exit_anchored<L>(top.open, top.begin, c.committed, row,
                            endsCascade ? top.state : kLrNoState);
  } else if (endsCascade) {
    c.tape.set_pending_state<L>(top.state);
  }
}

}  // namespace cursiv::engine
