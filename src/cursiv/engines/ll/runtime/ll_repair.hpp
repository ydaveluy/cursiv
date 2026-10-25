// What a parse does with text its grammar refuses. Every repair is local and
// every one of them leaves a record: a document too broken to read is still a
// tree, and an editor needs the tree more than it needs the verdict.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/ll/runtime/LlClimbExits.hpp"
#include "cursiv/engines/ll/runtime/LlRepairRun.hpp"
#include "cursiv/engines/ll/runtime/LlSection.hpp"
#include "cursiv/engines/ll/runtime/ll_lex_reading.hpp"
#include "cursiv/engines/ll/runtime/ll_note.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/ll/runtime/ll_search.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"

namespace cursiv {

// How far a repair may go on inventing without reading anything. A construct
// opened by an invented token calls the rules inside it, which may invent in
// turn; past this the descent stops filling positions and unwinds, and the
// text it never read is skipped instead.
inline constexpr std::uint16_t kLlInventRun = 16;

// Descent abandoned after the invention/depth limit still closes its regions.
// Keep that evidence on the record, even when no child could be written.
template <const auto& T>
inline NodeId ll_close(LlContext& c, CstBuilder::Open open, TextOffset begin,
                       std::uint32_t engine) {
  const NodeId id = c.tape.exit(open, begin, engine);
  if (c.give) {
    ll_mark(c, id);
    if (c.abandonDepth != ~std::size_t{0})
      T.stamp.set_row(c.tape.stream().at(id), kNoElement);
  }
  return id;
}

// ...and a record stating over a node its body builds, rather than opening
// one: given up before any record inside said anything, it says nothing,
// since no repair excuses a node that was not built.
template <const auto& T>
inline NodeId ll_close_over(LlContext& c, CstBuilder::Open open,
                            TextOffset begin, std::uint32_t engine) {
  const NodeId id = ll_close<T>(c, open, begin, engine);
  if (!c.give) [[likely]]
    return id;
  CstRun& run = c.tape.stream();
  const NodeId first = id + 1 - run.at(id).subtreeSize;
  for (NodeId k = id; k > first; k -= run.at(k - 1).subtreeSize)
    if (!run.at(k - 1).hidden && T.stamp.row(run.at(k - 1)) != kNoElement)
      return id;
  T.stamp.set_row(run.at(id), kNoElement);
  return id;
}

// Whether a nest turn has a receiver: what its region laid before it, which
// the turn's record reaches back over as its first child -- under a record of
// its own where it spans more than one, as the reference's composite does.
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

// A fault is reclassified at the token the point refused. Changing to the
// broad alphabet must not redo the preceding gap under another trivia
// configuration, which could hide the refused token or expose an earlier
// blank that the point had already skipped.
template <const auto& T>
inline void ll_repair_peek(LlContext& c, std::uint16_t mode) {
  if (!c.laValid) {
    ll_peek<T>(c, mode);
    return;
  }
  if (c.laMode == mode) return;
  if (c.la.sym == kEofSymbol ||
      (c.la.sym < T.nterms && T.terms[c.la.sym].laid != 0)) {
    c.laMode = mode;
    return;
  }
  const TextOffset p = c.la.begin;
  const Scan sc = ll_scan<T>(c, mode, p);
  c.la = Token{p, sc.end, sc.end == p ? kInvalidSymbol : sc.term};
  c.laMode = mode;
}

// A token owed and absent: the record spans nothing, and where it stands is
// where an editor puts the caret. The element names the TERMINAL, since what
// a repair inserts is bytes a token would match.
// The caret goes where the text stops, not where the next token starts: the
// trivia between them is the author's, and an insertion belongs in front of
// it. Nothing is flushed here for that reason -- the pending runs land after
// the record this writes.
template <const auto& T>
inline void ll_insert(LlContext& c, std::uint16_t term, std::uint16_t row,
                      std::uint16_t pos) {
  if (c.invented >= kLlInventRun) {
    c.repaired = true;
    c.give = true;
    return;
  }
  ++c.invented;
  // A RECORD NEVER GOES BEHIND THE TAPE. The trivia in front of the token may
  // already stand -- a group flushes it before it opens -- and a record laid
  // where the text stops would then sit behind it. The sentence still names
  // where the text stops, which is where an editor puts the caret.
  const CstRun& run = c.tape.stream();
  const TextOffset floor =
      run.records() != 0 ? run.at(run.records() - 1).end : 0;
  const TextOffset at = c.at > floor ? c.at : floor;
  LlRepairRun& f = ll_run<T>(c);
  if (!f.ins) {
    f.first = term;
    if (c.search && c.search->planned && c.search->next == 1 &&
        c.search->plan.front().kind == LlGesture::ins &&
        c.search->plan.front().term == term)
      f.also = c.search->also;
  }
  f.ins = true;
  c.last = c.tape.leaf(at, at, T.stamp.make(row, pos));
  ll_mark(c, c.last);
  if constexpr (kIndented<T>)
    if (indent_moves<T>(term)) {
      indent_shift_invented<T>(c.indentArena, c.indent, Token{at, at, term});
      c.laValid = false;
    }
}

// The token in hand, refused in its middle, read as the trivia its body owes:
// laid over the author's bytes up to `end`.
template <const auto& T>
inline void ll_skip(LlContext& c, std::uint16_t term, TextOffset end) {
  const TextOffset p = c.la.begin;
  ll_flush_trivia<T>(c);
  if (T.terms[term].recorded)
    ll_mark(c, c.tape.hidden(p, end, T.stamp.make(T.terms[term].element, 0)));
  c.repaired = true;
  LlRepairRun& f = ll_run<T>(c);
  if (!f.rep && !f.del) f.begin = p;
  f.rep = true;
  f.read = term;
  f.end = end;
  f.at = end;
  c.at = end;
  c.invented = 0;
  c.laValid = false;
  c.laMode = kInvalidSymbol;
}

// A token nothing wants: it goes on the tape as its own record, so the text is
// in the tree even where the grammar is not. A byte no terminal matches at all
// is dropped as one byte -- a skip that advanced by nothing would never end.
template <const auto& T>
inline void ll_drop(LlContext& c) {
  c.stop.poll();
  ll_flush_trivia<T>(c);
  TextOffset end = c.la.end;
  // A level a dropped token moves owns no byte past it.
  bool level = false;
  if constexpr (kIndented<T>)
    level = indent_drop(c.indentArena, c.indent.head, c.la,
                        kLaid<T, kLaidIndent>, kLaid<T, kLaidDedent>);
  if (!level && end == c.la.begin) end = ll_drop_end<T>(c, c.la);
  if (!level && end == c.la.begin) return;
  LlRepairRun& f = ll_run<T>(c);
  if (!f.del && !f.rep) f.begin = c.la.begin;
  const TextOffset span = level ? c.la.begin : end;
  f.del = true;
  f.end = span;
  f.at = end;
  f.resume = kNoElement;
  f.dropMode = ll_err_mode<T>(c);
  const NodeId id = c.tape.leaf(c.la.begin, span, T.stamp.make(kNoRow, 0));
  ll_mark(c, id);
  // No byte taken, no progress: the inventions before it still count.
  if (end != c.la.begin) c.invented = 0;
  c.at = end;
  c.laValid = false;
  c.laMode = kInvalidSymbol;
}

// A construct given up where it stands: the caller reads on from there.
template <const auto& T>
inline void ll_unfinished(LlContext& c, ElementId owed = kNoElement) {
  LlRepairRun& f = ll_run<T>(c);
  if (!f.abandon && !f.del && !f.rep) f.begin = f.end = f.point;
  f.abandon = true;
  f.owed = owed;
  c.repaired = true;
}

// The word the author spelled wrong, read as the terminal it spells wrong. The
// span stays his -- what a repair names is the terminal his bytes stand for.
template <const auto& T>
inline void ll_take_typo(LlContext& c, std::uint16_t mode, const Typo& t,
                         bool owed) {
  LlRepairRun& f = ll_run<T>(c);
  if (!f.del) f.begin = c.la.begin;
  f.rep = true;
  f.read = t.term;
  f.end = t.end;
  if (!owed) {
    f.spelt = c.la.begin;
    f.speltEnd = t.end;
  }
  c.la = Token{c.la.begin, t.end, t.term};
  c.laMode = mode;
  c.laValid = true;
  c.repaired = true;
}

// A gesture of the plan that reads or drops text, written wherever the descent
// stands; false for an insertion, which names the position that owes it.
template <const auto& T>
inline bool ll_write(LlContext& c, std::uint16_t mode, const LlGesture& g) {
  switch (g.kind) {
    case LlGesture::ins:
      return false;
    case LlGesture::del:
      ll_drop<T>(c);
      break;
    case LlGesture::rep:
      ll_take_typo<T>(c, mode, Typo{g.end, g.term, 1, true}, false);
      break;
    case LlGesture::skip:
      ll_skip<T>(c, g.term, g.end);
      break;
    case LlGesture::abandon: {
      if (g.end == 0 || g.end > c.stack.size()) {
        c.search->planned = false;
        return false;
      }
      const std::size_t keep = c.stack.size() - g.end;
      if (!T.atn.same_return(c.stack[keep], g.state)) {
        c.search->planned = false;
        return false;
      }
      const NodeCount from = c.stack.begin(keep);
      CstRun& run = c.tape.stream();
      LlRepairRun& f = ll_run<T>(c);
      f.abandon = true;
      f.begin = from < run.records() ? run.at(from).begin : c.at;
      f.end = c.at;
      f.owed = g.term;
      // The CST keeps the author's bytes; abandoned actions must no longer
      // create or attach a partial node while its generated frames close.
      for (NodeCount k = from; k < run.records(); ++k) {
        T.stamp.set_row(run.at(k), kNoElement);
        run.at(k).recovered = true;
      }
      c.give = true;
      c.abandonDepth = keep;
      c.repaired = true;
      break;
    }
  }
  ++c.search->next;
  return true;
}

// The plan in hand no longer says what this position owes: the next pause
// asks a search of its own.
inline void ll_unplan(LlContext& c) { c.search->planned = false; }

// One terminal, matched or repaired. A position that reads on after a
// gesture is filled, and the descent carries on -- a rule that gave up here
// would cost the tree everything after it.
template <const auto& T>
CURSIV_NOINLINE inline void ll_expect_repair(LlContext& c, std::uint16_t mode,
                                             std::uint16_t term,
                                             std::uint16_t row,
                                             std::uint16_t pos) {
  const LlRoot root{kNoRow, static_cast<std::uint16_t>(pos - 1)};
  bool again = true;
  for (;;) {
    // Read again under the alphabet that admits everything: a token this point
    // refuses is still a token, and a repair that could not see it whole would
    // invent one over bytes that are there.
    ll_repair_peek<T>(c, ll_err_mode<T>(c));
    const LlPause pause = pos != 0 ? ll_pause<T>(c, root)
                                   : LlPause{nullptr, kNoRow, true};
    if (const LlGesture* g = pause.gesture; g != nullptr) {
      if (g->kind == LlGesture::ins) {
        if (g->term == term) {
          ++c.search->next;
          ll_insert<T>(c, term, row, pos);
          return;
        }
      } else if (g->kind != LlGesture::rep || g->term == term) {
        const bool typo = g->kind == LlGesture::rep;
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
      // A step another position owes: the search is asked again from here.
      if (again && pos != 0) {
        again = false;
        ll_unplan(c);
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
        ll_take_typo<T>(c, mode, Typo{exact.end, term, 1, true}, true);
        ll_consume<T>(c, row, pos);
        ll_mark(c, c.last);
        return;
      }
    }
    // The first gesture that holds: the token dropped, or at the end of the
    // text the terminal invented.
    if (!ll_ends<T>(c, c.la.sym) && c.la.sym != kEofSymbol) {
      const TextOffset was = c.at;
      ll_drop<T>(c);
      if (c.at != was) {
        ll_peek<T>(c, mode);
        if (c.la.sym == term) {
          ll_consume<T>(c, row, pos);
          return;
        }
        continue;
      }
    }
    ll_insert<T>(c, term, row, pos);
    return;
  }
}

// Keep the sound path at its call site. The search and its many re-reads are
// shared per table, so a large grammar does not compile them into every leaf.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_expect(LlContext& c, std::uint16_t mode,
                                           std::uint16_t term,
                                           std::uint16_t row,
                                           std::uint16_t pos = 0) {
  if (c.give) return;
  ll_peek<T>(c, mode);
  if (c.la.sym == term) [[likely]]
    ll_consume<T>(c, row, pos);
  else
    ll_expect_repair<T>(c, mode, term, row, pos);
}

// How the climb of `decision` stands before `sym`: 1 where an operator takes
// it, 2 where the climb ends on it for a caller that goes on from it, 0 where
// neither does.
template <const auto& T>
inline int ll_climb_reads(LlContext& c, std::uint16_t decision,
                          std::uint16_t sym) {
  if (ll_ends<T>(c, sym)) return 2;
  LlSearch& s = ll_search_of<T>(c);
  s.sim.resume();
  s.sim.share_epsilon_contexts();
  s.sim.track_calls();
  s.sim.at_token(0);
  s.sim.keep_steps();
  const std::uint32_t state = T.decisions[decision].state;
  s.sim.start(state, ll_search_stack(c, s.sim, kLlSearchFrames));
  if (!s.sim.step(sym)) return 0;
  const auto exitArm = T.atn.state(state).nedges - 1u;
  for (const LlConfig& x : s.sim.configs())
    if (x.alt != exitArm) return 1;
  return 2;
}

// Whether the repairs of a choice are over: its alphabet reads the token in
// hand, or nothing more can be taken from the text.
template <const auto& T>
inline bool ll_settled(const LlContext& c, std::uint16_t decision) {
  return ll_ends<T>(c, c.la.sym) || T.holds(T.dec_first(decision), c.la.sym);
}

// A choice no arm of which takes the token in hand: the arm a repair enters
// with the token it owes invented in front, `kNoRow` where the text was
// repaired until an arm reads it or none of its arms reads on.
template <const auto& T>
CURSIV_NOINLINE inline std::uint16_t ll_choice_fix(LlContext& c,
                                                   std::uint16_t mode,
                                                   std::uint16_t decision) {
  const Token held = c.laMode == kLaHeld ? c.la : Token{};
  const auto done = [&](std::uint16_t arm) {
    ll_rehold<T>(c, held, mode);
    return arm;
  };
  const LlRoot root{decision, kNoRow};
  for (;;) {
    ll_repair_peek<T>(c, ll_err_mode<T>(c));
    const LlPause pause = ll_pause<T>(c, root);
    if (const LlGesture* g = pause.gesture; g != nullptr) {
      if (g->kind == LlGesture::ins) {
        ll_peek<T>(c, mode);
        return done(pause.arm);
      }
      ll_write<T>(c, mode, *g);
      if (c.give) return done(kNoRow);
    } else if (!pause.degraded) {
      ll_peek<T>(c, mode);
      return done(pause.arm);
    } else {
      if (ll_ends<T>(c, c.la.sym)) return done(kNoRow);
      const TextOffset was = c.at;
      ll_drop<T>(c);
      if (c.at == was) return done(kNoRow);
    }
    ll_peek<T>(c, mode);
    if (ll_settled<T>(c, decision)) return done(kNoRow);
  }
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

// ...and a loop that cannot go on from it: whether the repair takes another
// turn, or leaves the token to what follows the loop.
template <const auto& T>
CURSIV_NOINLINE inline std::uint8_t ll_loop_fix(LlContext& c,
                                                std::uint16_t mode,
                                                std::uint16_t decision) {
  const Token held = c.laMode == kLaHeld ? c.la : Token{};
  const auto done = [&](std::uint8_t fix) {
    ll_rehold<T>(c, held, mode);
    return fix;
  };
  const LlRoot root{decision, kNoRow};
  for (;;) {
    ll_repair_peek<T>(c, ll_err_mode<T>(c));
    const LlPause pause = ll_pause<T>(c, root);
    if (const LlGesture* g = pause.gesture; g != nullptr) {
      if (g->kind == LlGesture::ins)
        return done(pause.arm == 1 ? kLlLoopLeave : kLlLoopTurn);
      ll_write<T>(c, mode, *g);
      if (c.give) return done(kLlLoopLeave);
    } else if (!pause.degraded) {
      ll_peek<T>(c, mode);
      return done(pause.arm == 1   ? kLlLoopLeave
                  : pause.arm == 0 ? kLlLoopTurn
                                   : kLlLoopNone);
    } else {
      if (ll_ends<T>(c, c.la.sym)) return done(kLlLoopLeave);
      const TextOffset was = c.at;
      ll_drop<T>(c);
      if (c.at == was) return done(kLlLoopLeave);
    }
    ll_peek<T>(c, mode);
    if (ll_settled<T>(c, decision)) return done(kLlLoopNone);
  }
}

// A climb whose operand stands complete before a token it cannot go on from,
// and no caller can either: the operator a repair invents in front of that
// token, `kLlClimbAgain` where a skip left one the climb reads after all,
// anything else where it leaves.
inline constexpr std::uint16_t kLlClimbAgain = 0xFFFEu;

template <const auto& T>
CURSIV_NOINLINE inline std::uint16_t ll_climb_fix(LlContext& c,
                                                  std::uint16_t mode,
                                                  std::uint16_t decision) {
  const Token held = c.laMode == kLaHeld ? c.la : Token{};
  const auto done = [&](std::uint16_t arm) {
    ll_rehold<T>(c, held, mode);
    return arm;
  };
  const LlRoot root{decision, kNoRow};
  const std::uint16_t exitArm =
      static_cast<std::uint16_t>(T.atn.state(T.decisions[decision].state).nedges - 1u);
  bool moved = false;
  for (;;) {
    ll_repair_peek<T>(c, ll_err_mode<T>(c));
    const LlPause pause = ll_pause<T>(c, root);
    if (const LlGesture* g = pause.gesture; g != nullptr) {
      if (g->kind == LlGesture::ins) {
        if (pause.arm == exitArm || pause.arm == kNoRow) return done(kNoRow);
        ++c.search->next;
        return done(pause.arm);
      }
      ll_write<T>(c, mode, *g);
      if (c.give) return done(kNoRow);
      moved = true;
    } else if (!pause.degraded) {
      return done(kNoRow);
    } else {
      if (ll_ends<T>(c, c.la.sym)) return done(kNoRow);
      const TextOffset was = c.at;
      ll_drop<T>(c);
      if (c.at == was) return done(kNoRow);
      moved = true;
    }
    ll_peek<T>(c, mode);
    ll_repair_peek<T>(c, ll_err_mode<T>(c));
    if (const int reads = ll_climb_reads<T>(c, decision, c.la.sym); reads != 0)
      return done(moved && reads == 1 ? kLlClimbAgain : kNoRow);
  }
}

// What is left of the document once the entry rule has read what it can: text
// no rule wanted is still text, and the tree covers it.
template <const auto& T>
inline bool ll_tail(LlContext& c) {
  // The entry may end inside a region that reads whitespace as visible text:
  // the token it held is read again under the document's own trivia.
  c.laMode = kInvalidSymbol;
  ll_peek<T>(c, T.tailMode);
  if (c.la.sym == kEofSymbol) return true;
  ll_repair_peek<T>(c, ll_err_mode<T>(c));
  while (c.la.sym != kEofSymbol) {
    const TextOffset was = c.at;
    ll_drop<T>(c);
    if (c.at == was) break;
    ll_peek<T>(c, ll_err_mode<T>(c));
  }
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
// follows its entry: up to the end, or to the token that closes the section.
template <const auto& T>
inline void ll_drop_to_end(LlContext& c) {
  ll_repair_peek<T>(c, ll_err_mode<T>(c));
  while (!ll_ends<T>(c, c.la.sym)) {
    const TextOffset at = c.at;
    ll_drop<T>(c);
    if (c.at == at) break;
    ll_repair_peek<T>(c, ll_err_mode<T>(c));
  }
}

// An `eof` that a token may follow: the text stops here.
template <const auto& T>
inline void ll_end(LlContext& c, std::uint16_t mode) {
  ll_peek<T>(c, mode);
  if (!ll_ends<T>(c, c.la.sym)) ll_drop_to_end<T>(c);
}

// ...and left: what its entry did not read is dropped.
template <const auto& T>
inline void ll_section_close(LlContext& c, LlSection was) {
  ll_drop_to_end<T>(c);
  // The gap before the closing token belongs to the embedded grammar.
  // Commit it before returning to the caller's skip configuration: that
  // caller may re-read the delimiter under another alphabet, and must not
  // reclassify this gap or discard its recorded comments.
  ll_flush_trivia<T>(c);
  c.section = was.section;
}

}  // namespace cursiv
