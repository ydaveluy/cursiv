// What a parse does with text its grammar refuses. Every repair is local and
// every one of them leaves a record: a document too broken to read is still a
// tree, which an editor needs more than a pass or fail answer.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/CstRun.hpp"
#include "cursiv/utils/macros.hpp"
#include "cursiv/ll/runtime/LlClimbExits.hpp"
#include "cursiv/ll/runtime/LlRepairRun.hpp"
#include "cursiv/ll/runtime/LlSection.hpp"
#include "cursiv/ll/runtime/ll_lex_reading.hpp"
#include "cursiv/ll/runtime/ll_note.hpp"
#include "cursiv/ll/runtime/ll_read.hpp"
#include "cursiv/ll/runtime/ll_search.hpp"
#include "cursiv/ll/runtime/ll_section.hpp"
#include "cursiv/ll/table/LlTable.hpp"
#include "cursiv/shared/runtime/TypoWalk.hpp"

namespace cursiv::engine {

// How many tokens a repair may invent without reading where no plan asks: past
// this the descent unwinds and skips the text it never read. Plan insertions
// never count.
inline constexpr std::uint16_t kLlInventRun = 16;

// Whether the rule in hand was called before its caller wrote anything.
inline bool ll_called_first(const LlContext& c) {
  const std::size_t n = c.stack.size();
  return n == 0 || c.stack.begin(n - 1) == (n > 1 ? c.stack.begin(n - 2) : 0);
}

// A node that read no token where the text ends stands where the text before
// it stops, short of the trivia past it, unless it opens what calls it. Where
// trivia already stands past the text, the record stays after it.
template <const auto& T>
CURSIV_NOINLINE inline NodeId ll_exit_at_end(LlContext& c,
                                             CstBuilder::Open open,
                                             TextOffset begin,
                                             std::uint32_t engine) {
  const CstRun& run = c.tape.stream();
  const NodeCount n = run.records();
  if (c.laMode != kNoMode && !ll_called_first(c) &&
      (n == 0 || !run.at(n - 1).hidden))
    begin = n != 0 ? std::max(c.at, run.at(n - 1).end) : c.at;
  return c.tape.exit(open, begin, engine);
}

// A node's record, as the text bounds it.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline NodeId ll_exit(LlContext& c, CstBuilder::Open open,
                                           TextOffset begin,
                                           std::uint32_t engine) {
  if (c.at < begin && c.la.sym == kEofSymbol) [[unlikely]]
    return ll_exit_at_end<T>(c, open, begin, engine);
  return c.tape.exit(open, begin, engine);
}

// The nodes that closed on the end a region read span the gap it crossed to
// that end, which is the region's, unless trivia was written behind them.
inline void ll_span_region_end(LlContext& c) {
  if (c.regionMark == kNoNode) return;
  CstRun& run = c.tape.stream();
  bool trivia = false;
  for (NodeCount k = c.regionMark; k < run.records(); ++k)
    trivia = trivia || run.at(k).hidden;
  for (NodeCount k = c.regionMark; !trivia && k < run.records(); ++k)
    if (run.at(k).subtreeSize > 1 && run.at(k).end < c.regionEnd)
      run.at(k).end = c.regionEnd;
  c.regionMark = kNoNode;
}

// Descent abandoned after the invention/depth limit still closes its regions.
// Keep that evidence on the record, even when no child could be written.
template <const auto& T>
inline NodeId ll_close(LlContext& c, CstBuilder::Open open, TextOffset begin,
                       std::uint32_t engine) {
  const NodeId id = ll_exit<T>(c, open, begin, engine);
  if (c.give) {
    ll_mark(c, id);
    if (c.abandonDepth != ~std::size_t{0})
      T.stamp.set_row(c.tape.stream().at(id), kNoElement);
  }
  return id;
}

// A record stating over a node its body builds: given up before any inner
// record said anything, or holding only what an elected abandon gave up, it
// says nothing, since no node was built.
template <const auto& T>
inline NodeId ll_close_over(LlContext& c, CstBuilder::Open open,
                            TextOffset begin, std::uint32_t engine) {
  // Only a repair gives anything up.
  if (!c.repaired) [[likely]]
    return ll_exit<T>(c, open, begin, engine);
  const NodeId id = ll_close<T>(c, open, begin, engine);
  CstRun& run = c.tape.stream();
  const NodeId first = id + 1 - run.at(id).subtreeSize;
  const bool holds = first <= c.lost && c.lost < id;
  if (!c.give && !holds) return id;
  for (NodeId k = id; k > first; k -= run.at(k - 1).subtreeSize)
    if (!run.at(k - 1).hidden && T.stamp.row(run.at(k - 1)) != kNoElement)
      return id;
  T.stamp.set_row(run.at(id), kNoElement);
  // The call site reads it as a call that gave up.
  run.at(id).recovered = true;
  return id;
}

// Whether a nest turn has a receiver: what its region wrote before it, which
// the turn's record reaches back over as its first child, under a record of
// its own where it spans more than one.
template <const auto& T>
inline std::uint16_t ll_nest_receiver(LlContext& c, CstBuilder::Open region,
                                      TextOffset begin, NodeCount from) {
  const CstRun& run = c.tape.stream();
  unsigned seen = 0;
  for (NodeCount x = run.records(); x > from && seen < 2;
       x -= run.at(x - 1).subtreeSize)
    if (!run.at(x - 1).hidden) ++seen;
  if (seen == 0) return 0;
  if (seen > 1) ll_close<T>(c, region, begin, T.stamp.make(kNoRow, 0));
  return 1;
}

// A fault is reclassified at the token the point refused, without reading its
// gap again under other trivia. A token only a condition names stays: read
// again it would split into tokens the repair finds nothing wrong with.
template <const auto& T>
inline void ll_repair_peek(LlContext& c) {
  const std::uint16_t mode = ll_err_mode<T>(c.laMode, c.section);
  if (c.laMode == kNoMode) {
    ll_peek<T>(c, mode);
    return;
  }
  if (c.laMode == mode) return;
  if (c.la.sym == kEofSymbol ||
      (c.la.sym < T.nterms && (T.terms[c.la.sym].synthetic != 0 ||
                               !T.mode_admits(T.errMode, c.la.sym)))) {
    c.laMode = mode;
    return;
  }
  const TextOffset p = c.la.begin;
  const Scan sc = ll_scan<T>(c, mode, p);
  c.la = Token{p, sc.end, sc.end == p ? kInvalidSymbol : sc.term};
  c.laMode = mode;
}

// A missing token: a record spanning nothing where the text stops, where an
// editor puts the caret; the element names the terminal. The trivia after it is
// the author's, so nothing is flushed here.
template <const auto& T>
inline void ll_write_insert(LlContext& c, std::uint16_t term, std::uint16_t row,
                            std::uint16_t pos) {
  // A record never goes behind the tape: where the trivia before the token
  // already stands, the record goes after it, still naming where the text
  // stops.
  const CstRun& run = c.tape.stream();
  const TextOffset floor =
      run.records() != 0 ? run.at(run.records() - 1).end : 0;
  const TextOffset at = c.at > floor ? c.at : floor;
  LlRepairRun& f = ll_run<T>(c);
  if (!f.ins) {
    f.first = term;
    if (c.search && c.search->planned && c.search->next == 1 &&
        c.search->plan.front().kind == LlEdit::ins &&
        c.search->plan.front().term == term)
      f.also = c.search->also;
  }
  f.ins = true;
  c.last = c.tape.leaf(at, at, T.stamp.make(row, pos));
  ll_mark(c, c.last);
  if constexpr (kIndented<T>)
    if (indent_moves<T>(term)) {
      indent_shift_invented<T>(c.indentArena, c.indent, Token{at, at, term});
      c.laMode = kNoMode;
    }
}

// An insertion no plan asks for, within the run of inventions.
template <const auto& T>
inline void ll_insert(LlContext& c, std::uint16_t term, std::uint16_t row,
                      std::uint16_t pos) {
  if (c.invented >= kLlInventRun) {
    c.repaired.mark();
    c.give = true;
    return;
  }
  ++c.invented;
  ll_write_insert<T>(c, term, row, pos);
}

// The plan's insertion at this pause, written: the plan goes on past it.
template <const auto& T>
inline void ll_insert_planned(LlContext& c, std::uint16_t term,
                              std::uint16_t row, std::uint16_t pos) {
  ++c.search->next;
  ll_write_insert<T>(c, term, row, pos);
}

// The token in hand, refused in its middle, read as the trivia its body needs:
// written over the author's bytes up to `end` as the lexer writes that trivia.
template <const auto& T>
inline void ll_skip(LlContext& c, std::uint16_t term, TextOffset end) {
  const TextOffset p = c.la.begin;
  ll_flush_trivia<T>(c);
  if (T.terms[term].recorded)
    c.tape.hidden(p, end, T.stamp.make(T.terms[term].element, 0));
  c.repaired.mark();
  LlRepairRun& f = ll_run<T>(c);
  if (!f.rep && !f.del) f.begin = p;
  f.rep = true;
  f.read = term;
  f.end = end;
  f.at = end;
  c.at = c.triviaAt = end;
  c.heldEnd = end;
  c.invented = 0;
  c.laMode = kNoMode;
}

// A token nothing wants goes on the tape as its own record, so all the text is
// in the tree; a byte no terminal matches is dropped alone. Whether it moved: a
// byte, or a level a zero-width dedent closes.
template <const auto& T>
CURSIV_NOINLINE inline bool ll_drop(LlContext& c) {
  c.stop.poll();
  ll_flush_trivia<T>(c);
  TextOffset end = c.la.end;
  // A level a dropped token moves owns no byte past it.
  bool level = false;
  if constexpr (kIndented<T>)
    level = indent_drop(c.indentArena, c.indent.head, c.la,
                        kSyntheticTerm<T, kSyntheticIndent>,
                        kSyntheticTerm<T, kSyntheticDedent>);
  if (!level && end == c.la.begin) end = ll_drop_end<T>(c, c.la);
  if (!level && end == c.la.begin) return false;
  LlRepairRun& f = ll_run<T>(c);
  if (!f.del && !f.rep) {
    f.begin = c.la.begin;
    if constexpr (kIndented<T>)
      f.mixedIndent = indent_refused(c.src, c.len, c.indentArena, c.indent,
                                     c.at, c.la, [&c] { return c.heldEnd; });
  }
  const TextOffset span = level ? c.la.begin : end;
  f.del = true;
  f.end = span;
  f.at = end;
  f.resume = kNoElement;
  f.dropMode = ll_err_mode<T>(c.laMode, c.section);
  const NodeId id = c.tape.leaf(c.la.begin, span, T.stamp.make(kNoRow, 0));
  ll_mark(c, id);
  // No byte taken: the inventions before it still count.
  if (end != c.la.begin) {
    c.invented = 0;
    c.heldEnd = end;
  }
  c.at = c.triviaAt = end;
  c.laMode = kNoMode;
  return true;
}

// A construct given up where it stands: the caller reads on from there.
template <const auto& T>
inline void ll_unfinished(LlContext& c, ElementId missing = kNoElement) {
  LlRepairRun& f = ll_run<T>(c);
  if (!f.abandon && !f.del && !f.rep) f.begin = f.end = f.point;
  f.abandon = true;
  f.missing = missing;
  c.repaired.mark();
}

// The word the author spelled wrong, read as the terminal it spells wrong. The
// span stays the author's; the repair names the terminal those bytes stand for.
template <const auto& T>
inline void ll_take_typo(LlContext& c, std::uint16_t mode, const Typo& t,
                         bool required) {
  LlRepairRun& f = ll_run<T>(c);
  if (!f.del) f.begin = c.la.begin;
  f.rep = true;
  f.read = t.term;
  f.end = t.end;
  if (!required) {
    f.spelt = c.la.begin;
    f.speltEnd = t.end;
  }
  c.la = Token{c.la.begin, t.end, t.term};
  c.laMode = mode;
  c.repaired.mark();
}

// An edit of the plan that reads or drops text, written wherever the descent
// stands. An insertion is left to the position that needs it.
template <const auto& T>
inline void ll_write(LlContext& c, std::uint16_t mode, const LlEdit& g) {
  switch (g.kind) {
    case LlEdit::ins:
      return;
    case LlEdit::del:
      // Bytes no terminal matches end where the search read them to.
      if (c.la.end == c.la.begin) c.la.end = g.end;
      ll_drop<T>(c);
      break;
    case LlEdit::rep:
      ll_take_typo<T>(c, mode, Typo{g.end, g.term}, false);
      break;
    case LlEdit::skip:
      ll_skip<T>(c, g.term, g.end);
      break;
    case LlEdit::abandon: {
      if (g.end == 0 || g.end > c.stack.size()) {
        c.search->planned = false;
        return;
      }
      const std::size_t keep = c.stack.size() - g.end;
      if (!T.atn.same_return(c.stack[keep], g.state)) {
        c.search->planned = false;
        return;
      }
      const NodeCount from = c.stack.begin(keep);
      CstRun& run = c.tape.stream();
      LlRepairRun& f = ll_run<T>(c);
      f.abandon = true;
      f.begin = from < run.records() ? run.at(from).begin : c.at;
      f.end = c.at;
      f.missing = g.term;
      // The CST keeps the author's bytes as they stand; abandoned actions
      // must not create or attach a partial node while its generated frames
      // close, and no call site states over what was given up.
      for (NodeCount k = from; k < run.records(); ++k)
        T.stamp.set_row(run.at(k), kNoElement);
      c.last = kNoNode;
      c.give = true;
      c.abandonDepth = keep;
      c.lost = from;
      c.repaired.mark();
      break;
    }
  }
  ++c.search->next;
}

// The implied token of `condition`, where the condition holds. It stands before
// the gap, as an explicit separator would. A parent may already have flushed
// hidden trivia, so keep the tape monotone.
template <const auto& T>
inline bool ll_implied_holds(LlContext& c, std::uint16_t mode,
                             std::uint16_t condition, std::uint16_t row,
                             std::uint16_t pos) {
  if (!ll_condition<T>(c, condition, ll_normal_token<T>(c, c.la, mode), c.at))
    return false;
  const CstRun& run = c.tape.stream();
  const TextOffset floor =
      run.records() != 0 ? run.at(run.records() - 1u).end : 0;
  const TextOffset at = std::max(c.at, floor);
  c.last = c.tape.leaf(at, at, T.stamp.make(row, pos));
  return true;
}

// One terminal, matched or repaired: a position that reads on after an edit
// is filled and the descent goes on, an implied token's `condition` asked
// again past the last edit. Shared per table, not compiled into every leaf.
template <const auto& T>
CURSIV_NOINLINE inline void ll_expect(LlContext& c, std::uint16_t mode,
                                      std::uint16_t term, std::uint16_t row,
                                      std::uint16_t pos = 0,
                                      std::uint16_t condition = kNoRow) {
  if (c.give) return;
  ll_peek<T>(c, mode);
  if (c.la.sym == term) {
    ll_consume<T>(c, row, pos);
    return;
  }
  const LlRoot root{kNoRow, static_cast<std::uint16_t>(pos - 1)};
  bool again = true;
  for (;;) {
    // Read again under the alphabet of the point's configuration: a token this
    // point refuses is still a token, and a repair that could not see it whole
    // would invent one over bytes that are there.
    ll_repair_peek<T>(c);
    const LlPause pause = pos != 0 ? ll_pause<T>(c, root)
                                   : LlPause{nullptr, kNoRow, true};
    if constexpr (T.ngapConds != 0)
      if (condition != kNoRow && pause.edit == nullptr) {
        ll_peek<T>(c, mode);
        if (c.la.sym != term &&
            ll_implied_holds<T>(c, mode, condition, row, pos))
          return;
      }
    if (const LlEdit* g = pause.edit; g != nullptr) {
      if (g->kind == LlEdit::ins) {
        if (g->term == term) {
          ll_insert_planned<T>(c, term, row, pos);
          return;
        }
      } else if (g->kind != LlEdit::rep || g->term == term) {
        const bool typo = g->kind == LlEdit::rep;
        ll_write<T>(c, mode, *g);
        if (c.give) return;
        ll_peek<T>(c, mode);
        if (c.la.sym == term) {
          ll_consume<T>(c, row, pos);
          if (typo) ll_mark(c, c.last);
          return;
        }
        continue;
      }
      // A step another position needs: the search is asked again from here.
      if (again && pos != 0) {
        again = false;
        c.search->planned = false;
        continue;
      }
    } else if (!pause.degraded) {
      // The text reads here, as another alphabet reads it.
      ll_peek<T>(c, mode);
      if (c.la.sym == term) {
        ll_consume<T>(c, row, pos);
        return;
      }
      const Scan exact = ll_match_text<T>(c.src, c.len, mode, c.la.begin);
      if (exact.term == term && exact.end == c.la.end &&
          exact.end > c.la.begin) {
        ll_take_typo<T>(c, mode, Typo{exact.end, term}, true);
        ll_consume<T>(c, row, pos);
        ll_mark(c, c.last);
        return;
      }
    }
    // The first edit that holds: the token dropped, or at the end of the
    // text the terminal invented.
    if (!ll_ends<T>(c, c.la.sym) && c.la.sym != kEofSymbol && ll_drop<T>(c)) {
      ll_peek<T>(c, mode);
      if (c.la.sym == term) {
        ll_consume<T>(c, row, pos);
        return;
      }
      continue;
    }
    ll_insert<T>(c, term, row, pos);
    return;
  }
}

// Whether `mode` reads `term` off its one byte alone.
template <const auto& T>
constexpr bool ll_byte_reads(std::uint16_t mode, std::uint16_t term) {
  if constexpr (T.byteTerms == nullptr) {
    return false;
  } else {
    const std::string_view text = T.term_text(term);
    return text.size() == 1 &&
           T.byteTerms[T.modeBytes[mode] +
                       static_cast<unsigned char>(text[0])] == term;
  }
}

// The terminal a position expects. Nothing in hand, it is read in place: a
// one-byte literal at its byte past the blanks, any other token as `ll_peek`
// reads it. The token in hand is taken where `ll_peek` would keep it.
template <const auto& T, std::uint16_t Mode, std::uint16_t Term>
CURSIV_ALWAYS_INLINE inline void ll_expect(LlContext& c, std::uint16_t row,
                                           std::uint16_t pos = 0) {
  static_assert(T.mode_admits(Mode, Term));
  if (c.give) return;
  if (c.laMode == kNoMode) {
    bool read = false;
    if constexpr (ll_byte_reads<T>(Mode, Term)) {
      if (ll_in_place<T>(c)) {
        constexpr auto kByte = static_cast<unsigned char>(T.term_text(Term)[0]);
        const TextOffset p = ll_past_skip_run<T>(c, Mode, c.triviaAt);
        if (p < c.len && static_cast<unsigned char>(c.src[p]) == kByte) {
          c.pending.clear();
          c.pendEnd = p;
          c.la = Token{p, p + 1, Term, Mode};
          c.laMode = Mode;
          read = true;
        }
      }
    } else {
      read = ll_probe<T>(c, Mode);
    }
    if (!read) ll_peek_read<T>(c, Mode);
  }
  constexpr bool kPhysical =
      !kIndented<T> || (Term != kEofSymbol && T.terms[Term].synthetic == 0);
  if (c.la.sym == Term && (c.laMode == Mode || c.laMode == kLaHeld ||
                           (kPhysical && T.mode_covers(c.laMode, Mode))))
      [[likely]]
    ll_consume<T>(c, row, pos);
  else
    ll_expect<T>(c, Mode, Term, row, pos);
}

// An arm of a choice that reads one terminal and nothing else, as data: a
// choice of such arms selects one, and `ll_take_arm` reads it as the arm
// written in place would.
struct LlTermArm {
  std::uint16_t term, mode, row, pos;
};

// The arm a choice selected, taken where the token in hand is its terminal;
// where a repair chose the arm, matched or repaired.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_take_arm(LlContext& c, const LlTermArm& a) {
  if (c.la.sym == a.term) [[likely]]
    ll_consume<T>(c, a.row, a.pos);
  else
    ll_expect<T>(c, a.mode, a.term, a.row, a.pos);
}

// How the climb of `decision` stands before `sym`: 1 where an operator takes
// it, 2 where the climb ends on it for a caller that goes on from it, 0 where
// neither does.
template <const auto& T>
inline int ll_climb_reads(LlContext& c, std::uint16_t decision,
                          std::uint16_t sym) {
  if (ll_ends<T>(c, sym)) return 2;
  LlSearch& s = ll_search_of<T>(c);
  const std::uint32_t state = T.decisions[decision].state;
  s.sim.start(state, ll_search_open(c, s.sim, s.frames));
  if (!s.sim.step(sym)) return 0;
  const auto exitArm = T.atn.state(state).nedges - 1u;
  for (const LlConfig& x : s.sim.configs())
    if (x.alt != exitArm) return 1;
  return 2;
}

// Whether the repairs of a choice are over: its alphabet reads the token in
// hand, or nothing more can be taken from the text.
template <const auto& T>
inline bool ll_repairs_done(const LlContext& c, std::uint16_t decision) {
  return ll_ends<T>(c, c.la.sym) || T.holds(T.dec_first(decision), c.la.sym);
}

// The repairs of a choice no arm takes the token for, until a pause answers:
// `answer` reads a pause that elected an insertion or a reading, `give` answers
// a repair that gives up, `finished` whether the token in hand ends them.
template <const auto& T, class R, class Answer, class Finished>
CURSIV_ALWAYS_INLINE inline R ll_fix(LlContext& c, std::uint16_t mode,
                                     std::uint16_t decision, R give,
                                     Answer&& answer, Finished&& finished) {
  const Token held = c.laMode == kLaHeld ? c.la : Token{};
  const auto done = [&](R r) {
    ll_rehold<T>(c, held, mode);
    return r;
  };
  const LlRoot root{decision, kNoRow};
  for (;;) {
    ll_repair_peek<T>(c);
    const LlPause pause = ll_pause<T>(c, root);
    if (const LlEdit* g = pause.edit; g != nullptr && g->kind != LlEdit::ins) {
      ll_write<T>(c, mode, *g);
      if (c.give) return done(give);
    } else if (g != nullptr || !pause.degraded) {
      return done(answer(pause));
    } else if (ll_ends<T>(c, c.la.sym) || !ll_drop<T>(c)) {
      return done(give);
    }
    ll_peek<T>(c, mode);
    if (R r = give; finished(r)) return done(r);
  }
}

// A choice no arm of which takes the token in hand: the arm a repair enters,
// with the token it needs invented in front, `kNoRow` where the text was
// repaired until an arm reads it or none of its arms reads on.
template <const auto& T>
CURSIV_NOINLINE inline std::uint16_t ll_choice_fix(LlContext& c,
                                                   std::uint16_t mode,
                                                   std::uint16_t decision) {
  return ll_fix<T>(
      c, mode, decision, kNoRow,
      [&](const LlPause& pause) {
        ll_peek<T>(c, mode);
        return pause.arm;
      },
      [&](std::uint16_t&) { return ll_repairs_done<T>(c, decision); });
}

// A choice an arm of which reads the token in hand, though the prediction
// found none that reads what follows it: the arm whose reading repairs for the
// least.
template <const auto& T>
CURSIV_NOINLINE inline std::uint16_t ll_reading_fix(LlContext& c,
                                                    std::uint16_t decision) {
  if (c.give) return kNoRow;
  const LlPause pause = ll_pause<T>(c, LlRoot{decision, kNoRow});
  return pause.degraded ? kNoRow : pause.arm;
}

// A loop no turn of which the prediction answers leaves the token to a rule
// above that waits for it, unless a turn repairs for less than that rule does.
// The end of the text is waited for by all of them.
template <const auto& T>
CURSIV_NOINLINE inline bool ll_loop_leaves(LlContext& c,
                                           std::uint16_t decision) {
  if (c.give) return true;
  const LlPause pause = ll_pause<T>(c, LlRoot{decision, kNoRow});
  return pause.degraded ? ll_ends<T>(c, c.la.sym) : pause.arm == 1;
}

// What a repair at a loop answers: nothing, a turn, or to leave the loop.
inline constexpr std::uint8_t kLlLoopNone = 0, kLlLoopTurn = 1,
                              kLlLoopLeave = 2;

// A loop that cannot go on from the token in hand: whether the repair takes
// another turn, or leaves the token to what follows the loop.
template <const auto& T>
CURSIV_NOINLINE inline std::uint8_t ll_loop_fix(LlContext& c,
                                                std::uint16_t mode,
                                                std::uint16_t decision) {
  return ll_fix<T>(
      c, mode, decision, kLlLoopLeave,
      [&](const LlPause& pause) {
        if (pause.edit != nullptr)
          return pause.arm == 1 ? kLlLoopLeave : kLlLoopTurn;
        ll_peek<T>(c, mode);
        return pause.arm == 1   ? kLlLoopLeave
               : pause.arm == 0 ? kLlLoopTurn
                                : kLlLoopNone;
      },
      [&](std::uint8_t& fix) {
        fix = kLlLoopNone;
        return ll_repairs_done<T>(c, decision);
      });
}

// A climb whose operand is complete before a token no caller goes on from: the
// operator a repair invents there, `kLlClimbAgain` where a skip left one the
// climb reads, else none. A level too tight leaves it to the one it returns to.
inline constexpr std::uint16_t kLlClimbAgain = 0xFFFEu;

template <const auto& T>
CURSIV_NOINLINE inline std::uint16_t ll_climb_fix(LlContext& c,
                                                  std::uint16_t mode,
                                                  std::uint16_t decision) {
  const std::uint16_t exitArm =
      static_cast<std::uint16_t>(T.atn.state(T.decisions[decision].state).nedges - 1u);
  return ll_fix<T>(
      c, mode, decision, kNoRow,
      [&](const LlPause& pause) {
        return pause.edit != nullptr && pause.arm != exitArm ? pause.arm
                                                             : kNoRow;
      },
      [&](std::uint16_t& fix) {
        ll_repair_peek<T>(c);
        const int reads = ll_climb_reads<T>(c, decision, c.la.sym);
        if (reads == 1) fix = kLlClimbAgain;
        return reads != 0;
      });
}

// What is left of the document once the entry rule has read what it can: text
// no rule wanted is still text, and the tree covers it.
template <const auto& T>
inline bool ll_tail(LlContext& c) {
  // The entry may end inside a region that reads whitespace as visible text:
  // the gap behind it is the document's, so the token it held and the gap in
  // front of it are read again under the document's own trivia.
  c.laMode = kNoMode;
  ll_peek<T>(c, T.tailMode);
  if (c.la.sym == kEofSymbol) return true;
  ll_repair_peek<T>(c);
  while (c.la.sym != kEofSymbol && ll_drop<T>(c))
    ll_peek<T>(c, ll_err_mode<T>(c.laMode, c.section));
  return false;
}

// A section entered: the descent reads its grammar alone.
template <const auto& T>
inline LlSection ll_section_open(LlContext& c, std::uint8_t section) {
  const LlSection was{c.section};
  c.section = section;
  return was;
}

// What stands before the end of the text, as a grammar alone drops what
// follows its entry: up to the end, or to the token that closes the section,
// each token read as the first fault is.
template <const auto& T>
inline void ll_drop_to_end(LlContext& c) {
  ll_repair_peek<T>(c);
  const std::uint16_t mode = c.laMode;
  while (!ll_ends<T>(c, c.la.sym) && ll_drop<T>(c)) ll_peek<T>(c, mode);
}

// An `eof` that a token may follow: the text stops here. Read under a
// region's trivia, the gap before it is that region's.
template <const auto& T>
inline void ll_end(LlContext& c, std::uint16_t mode) {
  ll_peek<T>(c, mode);
  if (!ll_ends<T>(c, c.la.sym)) ll_drop_to_end<T>(c);
  if constexpr (T.modeTrivia != nullptr)
    if (T.modeTrivia[mode] != T.modeTrivia[T.tailMode] &&
        ll_ends<T>(c, c.la.sym)) {
      c.regionEnd = c.la.begin;
      c.regionMark = c.tape.stream().records();
    }
}

// A section left: what its entry did not read is dropped.
template <const auto& T>
inline void ll_section_close(LlContext& c, LlSection was) {
  ll_span_region_end(c);
  ll_drop_to_end<T>(c);
  // The gap before the closing token belongs to the embedded grammar: it is
  // flushed before the caller's configuration reads the delimiter again.
  ll_flush_trivia<T>(c);
  c.section = was.section;
}

}  // namespace cursiv::engine
