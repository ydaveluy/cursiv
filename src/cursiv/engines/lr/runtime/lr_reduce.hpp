// The reduction shared by generated states and repair replay: production and
// landing are constants on the nominal path, table values in the writer.
#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
#include "cursiv/engines/lr/runtime/lr_moves.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/lr/table/LrReduceForm.hpp"
#include "cursiv/engines/lr/table/LrReducePlan.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

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

// Prepare the result frame, leaving its first state's tag intact for a
// guarded goto. The returned row is a record the landing must close. A
// production passed as an integral constant unrolls its renames, which a loop
// over the table does not at -O2.
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
  // A turn's record reaches back over what the frame below it laid. Grouping
  // that slips a record in, so it waits for the renames above.
  const bool reach =
      plan.turnRow != kNoElement && p.len != 0 &&
      lr_receiver<A.stamp>(c, c.stack[base - 1].open, c.stack[base].open,
                           c.stack[base - 1].begin);
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
  c.stack.reduce(p.len);
  // The result takes the slot of the first symbol it pops, whose span is
  // already its own: only an empty production, or a turn reaching the frame
  // below, opens elsewhere.
  LrFrame& top = c.stack.top();
  if (p.len == 0) {
    top.open = c.tape.len();
    top.begin = c.committed;
  } else if (reach) {
    top.open = c.stack[base - 1].open;
    top.begin = c.stack[base - 1].begin;
  }
  top.row = pendRow;
  top.word = last ? pendWord : kLrNoWord;
  assert((plan.row(p.len) == LrReduceRow::none) == (pendNode == kNoElement) ||
         plan.row(p.len) == LrReduceRow::any);
  return pendNode;
}

// `settles`: the landing state ends the reduction cascade.
template <StampLayout L>
CURSIV_ALWAYS_INLINE inline void lr_reduce_finish(LrContext& c,
                                                  std::uint32_t to,
                                                  ElementId row, bool settles) {
  LrFrame& top = c.stack.top();
  top.state = static_cast<std::uint16_t>(to);
  if (row != kNoElement) {
    top.word = c.tape.len();
    c.tape.exit_anchored<L>(top.open, top.begin, c.committed, row,
                            settles ? top.state : kLrNoState);
  } else if (settles) {
    c.tape.settle<L>(top.state);
  }
}

}  // namespace cursiv
