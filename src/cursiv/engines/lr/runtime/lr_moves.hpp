// The tape and stack moves the emitted parser calls, shared by every grammar
// in the program: nothing here names a table but the stamp layout, which the
// emitted parser spells only where its table narrowed the row.
#pragma once

#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// Only a hidden rule's run becomes a record. Out of line: inlined, the loop's
// registers are saved on every shift, and half the shifts record no run.
template <StampLayout L>
CURSIV_COLD CURSIV_NOINLINE inline void lr_record_pending(
    LrContext& c, const ElementId* termElem) {
  for (const Token& t : c.pending)
    c.tape.hidden<L>(t.begin, t.end, termElem[t.sym]);
  c.pending.clear();
}
template <bool Recorded, StampLayout L = StampLayout{}>
CURSIV_ALWAYS_INLINE inline void lr_record_trivia(
    LrContext& c, [[maybe_unused]] const ElementId* termElem) {
  if constexpr (Recorded) {
    if (!c.pending.empty()) [[unlikely]]
      lr_record_pending<L>(c, termElem);
  }
}

// The cursor past what the matcher held. A shift moves `committed` itself and
// calls the records alone.
template <bool Recorded, StampLayout L = StampLayout{}>
CURSIV_ALWAYS_INLINE inline void lr_flush(LrContext& c,
                                          const ElementId* termElem) {
  lr_record_trivia<Recorded, L>(c, termElem);
  if (c.pendEnd > c.committed) c.committed = c.pendEnd;
}

// The leaf onto the tape and its frame onto the stack.
template <StampLayout L = StampLayout{}>
CURSIV_ALWAYS_INLINE inline void lr_push_leaf(LrContext& c,
                                              std::uint32_t target,
                                              ElementId leaf, TextOffset begin,
                                              TextOffset end) {
  // The chain the leaf carries stays on the frame, for a directive at the
  // call site to fold onto.
  c.stack.push(LrFrame{static_cast<std::uint16_t>(target), leaf, c.tape.len(),
                       begin, c.tape.len()});
  // The state after the shift is what a caret replays the tape from.
  c.tape.leaf<L>(begin, end, leaf, static_cast<std::uint16_t>(target));
  c.committed = end;
  c.laMode = kNoMode;
  ++c.shifts;  // what proves a repair, and what validates the next
}

// Whether `word`'s record still ends the derivation: past it stands only what
// owes the tree nothing -- trivia, and recovered regions holding no chain.
template <StampLayout L = StampLayout{}>
CURSIV_NOINLINE inline bool lr_last_real_word(const LrContext& c,
                                              std::uint32_t word) {
  std::uint32_t w = c.tape.len();
  while (w > word + 1) {
    const CstNode& r = c.tape.at(w - 1);
    if (r.hidden) {
      --w;
      continue;
    }
    if (!r.recovered) return false;
    for (std::uint32_t k = w - r.subtreeSize; k < w; ++k) {
      const CstNode& s = c.tape.at(k);
      if (!s.hidden && L.row(s) != kNoElement) return false;
    }
    w -= r.subtreeSize;
  }
  return w == word + 1;
}

// Where the record is a creation, the directive does not replace it: `folds`
// names the row the table minted for both, or `kNoElement`.
template <StampLayout L = StampLayout{}, class Folds>
CURSIV_ALWAYS_INLINE inline void lr_name_row(LrContext& c, LrFrame& f,
                                             ElementId site, Folds folds) {
  if (f.word == kLrNoWord) return;  // the symbol wrote no record of its own
  ElementId row = site;
  // What the record now states stays on the frame: the next site folds onto
  // it rather than renaming it away.
  if (f.row != kNoElement) {
    const ElementId one = folds(f.row, site);
    if (one != kNoElement) row = one;
  }
  c.tape.rename<L>(f.word, row);
  f.row = row;
}

// The same, for a leaf: its pair was folded at build time.
template <StampLayout L = StampLayout{}>
CURSIV_ALWAYS_INLINE inline void lr_name_row_at(LrContext& c, LrFrame& f,
                                                ElementId row) {
  if (f.word == kLrNoWord) return;  // the symbol wrote no record of its own
  c.tape.rename<L>(f.word, row);
  f.row = row;
}

}  // namespace cursiv
