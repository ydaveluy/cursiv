// The parse's moves read off the table instead of the generated states: what a
// repair writes where the drive paused. A reduce writes what the generated one
// writes, so a reading replayed here writes the drive's own records.
#pragma once

#include <cstdint>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrDrive.hpp"
#include "cursiv/lr/runtime/LrFrame.hpp"
#include "cursiv/lr/runtime/LrPin.hpp"
#include "cursiv/lr/runtime/LrTape.hpp"
#include "cursiv/lr/runtime/lr_guard.hpp"
#include "cursiv/lr/runtime/lr_lex_reading.hpp"
#include "cursiv/lr/runtime/lr_matcher.hpp"
#include "cursiv/lr/runtime/lr_moves.hpp"
#include "cursiv/lr/runtime/lr_read.hpp"
#include "cursiv/lr/runtime/lr_reduce.hpp"
#include "cursiv/lr/runtime/lr_rescan.hpp"
#include "cursiv/lr/runtime/lr_states.hpp"
#include "cursiv/lr/table/LrAction.hpp"
#include "cursiv/lr/table/LrActionKind.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/runtime/LexReading.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

// The trivia before the token in hand, written as the drive writes it.
template <const auto& A>
inline void lr_write_trivia(LrContext& c) {
  lr_record_trivia<kTermsRecorded<A.cfg>, A.stamp>(c, kLrTermElem<A>.data());
}

// The goto decides first (a guarded one by the tag of the span's first state,
// or by `holds` where the span is empty): one that leads to no state writes
// nothing.
template <const auto& A, class Holds>
bool lr_write_reduce(LrContext& c, std::uint16_t prod, Holds&& holds) {
  const auto& p = A.cfg.prods[prod];
  if (p.len >= c.stack.size()) return false;
  const std::uint32_t base = c.stack.size() - p.len;
  const std::uint32_t first = p.len != 0 ? c.stack[base].state : kLrNoFirst;
  const std::uint32_t to =
      lr_reduce_target<A>(c.stack[base - 1].state, prod, first, holds);
  if (to == kInvalidSymbol) return false;
  const ElementId row = lr_reduce_frame<A>(
      c, prod,
      [&](ElementId held, ElementId over) { return c.drive->folds(held, over); });
  lr_reduce_finish<A.stamp>(c, to, row, A.cold.readsToken[to] != 0);
  return true;
}

// The token a repair puts in hand where the drive paused, and the context's
// own stack, cursor and levels it is read over.
struct LrWriteReading : LrPin {
  struct Run {
    const LrStack* s;
    [[nodiscard]] std::uint32_t back() const { return s->top().state; }
  };
  Run run;
  Token la{};
  std::uint16_t laMode = kNoMode;
  bool done = false;
  LrVirtualAt virt;
  IndentAt indent;
};

// The interpreted writer starts from the context the drive left in hand.
inline LrWriteReading lr_write_reading(const LrContext& c) {
  LrWriteReading w;
  w.run = {&c.stack};
  w.la = c.la;
  w.laMode = c.laMode;
  w.virt = c.virt;
  w.indent = c.indent;
  return w;
}

// One action written for the reading: a shift writes its leaf (the held token
// as a repaired one), and a reduce what the generated one writes.
template <const auto& A>
bool lr_write_apply(LrContext& c, LrWriteReading& w, std::uint32_t at) {
  const LrAction x = lr_shadow_action<A>(at);
  if (lr_takes_token_in<A>(x.kind)) {
    const std::uint32_t to =
        lr_shift_target<A>(x.kind, x.arg, lr_shadow_holds(c, w));
    if constexpr (kLrGuarded<A>)
      if (x.kind == LrActionKind::guard && to == kInvalidSymbol) return false;
    const auto state = static_cast<std::uint16_t>(to);
    const ElementId row = A.cfg.terms[w.la.sym].element;
    const bool held = w.pin && w.la == w.pinTok;
    // An implied token is inserted where the last token ended, as the drive
    // inserts it, and the trivia past it is read again behind it.
    const bool implied =
        kLrImplied<A>.size() != 0 && !held && w.virt.implies(w.la);
    if (implied) {
      lr_push_leaf<A.stamp>(c, to, row, c.committed, c.committed);
      c.pending.clear();
    } else if (held && !w.pinRep) {
      // Before the first token, the comments in front of the text stand
      // outside it: what the repair adds goes after them.
      if (c.shifts == 0) {
        lr_write_trivia<A>(c);
        if (c.tape.held() > c.committed) c.committed = c.tape.held();
      }
      c.stack.push(
          LrFrame{state, row, c.tape.len(), c.committed, c.tape.len()});
      c.tape.repaired_leaf(c.committed, c.committed, A.stamp.make(row, state));
    } else if (held) {
      lr_write_trivia<A>(c);
      c.stack.push(
          LrFrame{state, row, c.tape.len(), w.la.begin, c.tape.len()});
      c.tape.repaired_leaf(w.la.begin, w.la.end, A.stamp.make(row, state));
      c.committed = w.la.end;
      ++c.shifts;
    } else {
      lr_write_trivia<A>(c);
      lr_push_leaf<A.stamp>(c, to, row, w.la.begin, w.la.end);
    }
    if constexpr (kIndented<A.cfg>) lr_read_indent_shift<A>(c, w);
    w.virt.shift(w.la);
    w.laMode = kNoMode;
    if (held) {
      w.pin = false;
      if (!w.pinRep) {
        w.la = w.held;
        w.laMode = w.heldMode;
      }
    }
    return true;
  }
  if (x.kind == LrActionKind::accept) {
    w.done = true;
    return true;
  }
  if (!lr_reduce_allowed<A>(x.arg, lr_shadow_holds(c, w))) return false;
  return lr_write_reduce<A>(c, x.arg, lr_shadow_holds(c, w));
}

// A token the author did not write, with the reductions the table makes on
// it, taking the arms `arms` names where it forks. False where the table
// refuses it.
template <const auto& A>
bool lr_write_token(LrContext& c, SymbolId term, bool reread, TextOffset end,
                    const Token& written, const std::uint32_t* arms,
                    std::uint32_t narms) {
  LrWriteReading w = lr_write_reading(c);
  lr_pin(w, term, reread, end, written, A.mode[c.stack.top().state]);
  std::uint32_t used = 0;
  while (w.pin && !w.done) {
    const auto actions = lr_read_actions<A>(
        c, [&]() -> LrWriteReading& { return w; },
        [&] {
          // No text is scanned during replay: the pinned token remains in
          // hand, with an implied token the gap inserted ahead of it, if any.
          w.la = w.pinTok;
          if constexpr (kLrImplied<A>.size() != 0)
            if (w.virt.pending != 0) w.la = w.virt.in_hand();
          w.laMode = A.mode[c.stack.top().state];
        },
        [] {});
    if (actions.count == 0) return false;
    std::uint32_t arm = 0;
    if (actions.count > 1 && used < narms) arm = arms[used++];
    if (arm >= actions.count) arm = 0;
    if (!lr_write_apply<A>(c, w, actions.first + arm)) return false;
  }
  c.virt = w.virt;
  c.indent = w.indent;
  c.la = w.la;
  c.laMode = w.laMode;
  return true;
}

// The extent a deletion takes of the token in hand: its own, or, where its mode
// read none, the longest reading or refusal of the mode that lexes everything,
// else the unmatched text. The walks stop at `end`; `reached` rises with them.
template <const auto& A>
TextOffset lr_error_end(const LrContext& c, std::uint16_t mode, const Token& t,
                        TextOffset end, TextOffset& reached) {
  if (t.begin >= c.len) return t.begin;
  if (t.sym != kInvalidSymbol) {
    if (t.sym < A.cfg.nterms && A.cfg.terms[t.sym].synthetic != 0)
      return t.begin;
    return t.end > t.begin ? t.end : t.begin + 1;
  }
  const auto open = [&](std::uint16_t m) {
    if (c.reads.spent()) return LexReading{};
    TextOffset read = t.begin;
    std::uint64_t closer = 0;
    const LexReading r =
        lr_open_reading<A>(c, mode, m, t.begin, end, read, closer);
    c.reads.spend(t.begin, read + static_cast<TextOffset>(closer));
    if (read > reached) reached = read;
    return r;
  };
  if (const LexReading r = open(mode); r.term != kInvalidSymbol)
    return lex_cut(t.begin, r);
  TextOffset halt = t.begin;
  const std::uint16_t err = lr_err_mode<A>(mode);
  const Scan s = lr_match_halt<A>(c.src, end, err, t.begin, halt);
  if (halt > reached) reached = halt;
  if (s.term != kInvalidSymbol && s.end > t.begin) return s.end;
  if (const LexReading r = open(err); r.term != kInvalidSymbol)
    return lex_cut(t.begin, r);
  return lr_unmatched_end<A>(c.src, end, mode, t.begin, reached, c.reads,
                             [&c] { c.stop.poll(); });
}

template <const auto& A>
TextOffset lr_error_end(const LrContext& c, std::uint16_t mode,
                        const Token& t) {
  TextOffset reached = t.begin;
  return lr_error_end<A>(c, mode, t, c.len, reached);
}

// The token in hand as the mode that lexes everything reads it where its own
// read nothing, in a section under its grammar's: what conditions ask about a
// re-reading of it.
template <const auto& A>
CURSIV_ALWAYS_INLINE inline Token lr_written(const LrContext& c, const Token& t,
                                             std::uint16_t mode) {
  if (t.sym != kInvalidSymbol || t.begin >= c.len) return t;
  const Scan s = c.drive->walk(c.src, c.len, lr_err_mode<A>(mode), t.begin);
  if (s.term != kInvalidSymbol && s.end > t.begin)
    return Token{t.begin, s.end, s.term};
  return t;
}

// Token `t` dropped: an implied one leaves `virt.pending`, an indent still
// opens its level, muted, and a dedent closes the level under the muted ones.
template <const auto& A>
void lr_drop_levels(LrContext& c, LrVirtualAt& virt, IndentAt& indent,
                    const Token& t) {
  if (t.sym == virt.pending && virt.pending != 0) virt.pending = 0;
  if constexpr (kIndented<A.cfg>)
    indent_drop(c.indentArena, indent.head, t,
                kSyntheticTerm<A.cfg, kSyntheticIndent>,
                kSyntheticTerm<A.cfg, kSyntheticDedent>);
}

// A state that reads no token reduces one production on every token: its
// construct is whole, so it closes before a repair's record, which stands
// outside it. Not where the levels may insert a zero-width token first.
template <const auto& A>
void lr_close_whole(LrContext& c, TextOffset resume) {
  if constexpr (!kIndented<A.cfg>) {
    for (;;) {
      const std::uint32_t state = c.stack.top().state;
      const std::uint16_t p = A.cold.soleReduce[state];
      if (A.cold.readsToken[state] != 0 || p == kLrNoProd ||
          A.cfg.prods[p].gap != 0 || A.cfg.prods[p].len == 0)
        return;
      const Token next =
          lr_scan_at<A>(c, A.mode[state], resume, c.indent.depth);
      if (next.sym == kInvalidSymbol) return;
      std::uint32_t first = 0;
      unsigned count = 0;
      lr_action_run<A>(state, next.sym, first, count);
      if (count != 1 ||
          lr_shadow_action<A>(first).kind != LrActionKind::reduce ||
          !lr_write_reduce<A>(c, p, [](unsigned) { return false; }))
        return;
    }
  } else {
    (void)c, (void)resume;
  }
}

// Drop the token in hand, up to `end`: an error leaf over the author's bytes,
// or, for a token the parse inserted, the level it moved.
template <const auto& A>
void lr_write_delete(LrContext& c, TextOffset end) {
  const Token t = c.la;
  lr_drop_levels<A>(c, c.virt, c.indent, t);
  lr_close_whole<A>(c, end > t.begin ? end : t.begin);
  lr_flush_trivia<A>(c);
  const TextOffset at = t.begin > c.committed ? t.begin : c.committed;
  c.tape.repaired_leaf(at, end > at ? end : at, A.stamp.make(kNoRow, 0));
  if (end > c.committed) c.committed = end;
  if (end != t.begin) c.virt.from = end;
  c.laMode = kNoMode;
}

// A trivia the lexer refused, written up to `end` as the lexer writes that
// trivia: the gap before the next token still reaches over it.
template <const auto& A>
void lr_write_skip(LrContext& c, SymbolId term, TextOffset end) {
  const TextOffset at = c.la.begin > c.committed ? c.la.begin : c.committed;
  lr_close_whole<A>(c, end);
  lr_flush_trivia<A>(c);
  if constexpr (kTermsRecorded<A.cfg>)
    if (kLrTermMask<A, true>.test(term))
      c.tape.hidden<A.stamp>(at, end, kLrTermElem<A>[term]);
  if (end > c.committed) c.committed = end;
  c.laMode = kNoMode;
}

// Give up the frames above `keep` and the text they read: one node over it,
// recovered, naming the state that resumes where there is text under it.
template <const auto& A>
void lr_write_abandon(LrContext& c, std::uint32_t keep, ElementId row) {
  const NodeCount len = c.tape.len();
  const NodeCount from = keep < c.stack.size() ? c.stack[keep].open : len;
  const TextOffset at =
      keep < c.stack.size() ? c.stack[keep].begin : c.committed;
  const auto retained = static_cast<std::uint16_t>(
      row == kNoRow && len > from ? c.stack[keep - 1].state : 0);
  c.stack.truncate(keep);
  if (c.tape.pending_close() != kLrNoPending && c.tape.pending_close() >= from)
    c.tape.pending_close(kLrNoPending);
  c.tape.repaired_close(at, c.committed, len - from + 1,
                        A.stamp.make(row, retained));
  if (c.la.sym == kInvalidSymbol) {
    c.laMode = kNoMode;
    c.pending.clear();
  }
}

}  // namespace cursiv::engine
