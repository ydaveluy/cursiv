// The repair search: best-first over the gestures a refused reading can make,
// each reading read ahead by the cells' own step until three tokens hold it.
// Among the cheapest, the one read furthest on is elected, and its path is
// written one gesture per pause.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/core/utils/saturating.hpp"
#include "cursiv/engines/lr/runtime/LrCactus.hpp"
#include "cursiv/engines/lr/runtime/LrCactusClimb.hpp"
#include "cursiv/engines/lr/runtime/LrConfig.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrExit.hpp"
#include "cursiv/engines/lr/runtime/LrGesture.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/lr/runtime/LrSearch.hpp"
#include "cursiv/engines/lr/runtime/lr_read.hpp"
#include "cursiv/engines/lr/runtime/lr_repair_write.hpp"
#include "cursiv/engines/lr/runtime/lr_search_bound.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/RepairFault.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// What a link is to the expected set: an arm the drive took is no gesture; a
// gesture inserts its terminal, or nothing.
inline std::uint32_t lr_search_kind(const LrGesture& g) {
  if (g.kind == LrGesture::arm) return TiedInserts::kArm;
  return g.kind == LrGesture::ins ? std::uint32_t{g.term}
                                  : std::uint32_t{kNoElement};
}

inline bool lr_search_goal(const LrConfig& r) {
  return r.done || r.shifts >= kRepairValidateShifts;
}

inline void lr_search_queue(LrSearch& s, std::uint32_t f, const LrConfig& r,
                            std::uint32_t at, std::uint32_t lost = 0) {
  s.queue(s.configs, f, r, at, lost,
          !r.generates() && !lr_search_goal(r) && r.fresh);
}

// Once a gesture's readings are admitted, the tail no queue entry names is
// given back; where none is left and none handed its link on, so are the
// links and cactus nodes they laid, which only they named.
inline void lr_search_trim(LrSearch& s, const LrSearchLaid& m) {
  while (s.configs.size() > m.configs && !s.configs.back().queued)
    s.configs.pop_back();
  if (s.configs.size() != m.configs || s.handed != m.handed) return;
  s.links.resize(m.links);
  s.cactus.rewind(m.cactus);
}

// Reading `i` queued, unless the one its future keeps outdoes it.
inline void lr_search_admit(LrSearch& s, std::uint32_t i) {
  LrConfig& r = s.configs[i];
  assert(!r.generates());
  const std::uint32_t h =
      !lr_search_goal(r) && r.freeInserts == 0 ? kRepairReread : 0;
  const auto [it, fresh] = s.seen.try_emplace(LrSearch::Future::of(r), i);
  if (!fresh && repair_meet<&LrConfig::freeInserts>(s, s.configs, i, it->second,
                                                    lr_search_kind))
    return;
  lr_search_queue(s, r.cost + h, r, i);
}

// The readings in `s.out` admitted, and what the gesture laid trimmed.
CURSIV_ALWAYS_INLINE inline void lr_search_admit_out(LrSearch& s,
                                                     const LrSearchLaid& m) {
  for (const std::uint32_t q : s.out) lr_search_admit(s, q);
  lr_search_trim(s, m);
}

// A fork is a reading of its own at no cost, ranked by the arm it takes.
template <const auto& A>
struct LrSearchPool {
  LrContext& c;
  LrSearch& s;
  // The reading itself is the only one read: no arm beside it is ever taken.
  bool single = false;

  LrConfig& operator[](std::size_t i) { return s.configs[i]; }
  std::size_t fork(std::size_t i) {
    if (single) return kLrNoFork;
    LrConfig& r = s.configs[i];
    if (r.armNext == 0) {
      r.link = s.link(LrGesture{.parent = r.link, .held = r.pin});
      r.armNext = 1;
    }
    LrConfig copy = r;
    copy.queued = false;
    copy.link = s.link(LrGesture{.parent = s.links[copy.link].parent,
                                 .choice = copy.armNext,
                                 .held = copy.pin});
    ++r.armNext;
    ++s.work;
    const auto j = static_cast<std::uint32_t>(s.configs.size());
    s.configs.push_back(copy);
    s.reading.push_back(j);
    return j;
  }
  void drop(std::size_t j) { s.configs[j].gone = true; }
  // An action refused leaves the reading as it stood.
  template <class Act>
  bool attempt(std::size_t i, Act act) {
    ++s.work;
    LrConfig& r = s.configs[i];
    const LrCactusClimb run = r.run;
    const TextOffset was = r.at;
    if (act(r)) return true;
    r.run = run;
    r.at = was;
    return false;
  }
  bool apply(std::size_t i, std::uint32_t at) {
    return attempt(i,
                   [&](LrConfig& r) { return lr_shadow_apply<A>(c, r, at); });
  }
  bool reduce(std::size_t i, std::uint16_t p) {
    return attempt(i,
                   [&](LrConfig& r) { return lr_shadow_reduce<A>(c, r, p); });
  }
  // Every reading at a position reads the same token, a bracket's depth aside.
  Token scan(std::size_t i, std::uint16_t mode) {
    const LrConfig& r = s.configs[i];
    const std::uint64_t key = (std::uint64_t{r.at} << 17) |
                              (std::uint64_t{mode} << 1) |
                              (r.indent.depth != 0 ? 1u : 0u);
    if (const auto it = s.lexed.find(key); it != s.lexed.end())
      return it->second;
    ++s.work;
    const Token t = lr_scan_at<A>(c, mode, r.at, r.indent.depth);
    s.lexed.emplace(key, t);
    return t;
  }
  void lexed() {}
  void kept_at(std::size_t) {}
  void stepped() {}
};

// What the search walked between its units of work since it last paid, at a
// lexed byte's rate: the bytes the conditions read of long gaps, and the
// siblings the cactus stepped past.
inline void lr_search_pay(const LrContext& c, LrSearch& s) {
  s.work += s.cactus.pay(kRepairLexBytes);
  const std::uint64_t read = c.gaps.read() - s.gapsPaid;
  if (read < kRepairLexBytes) return;
  s.work += read / kRepairLexBytes;
  s.gapsPaid += read - read % kRepairLexBytes;
}

// Its forks go to `s.reading`.
template <const auto& A>
CURSIV_ALWAYS_INLINE inline void lr_search_step(LrContext& c,
                                                LrSearchPool<A>& x,
                                                std::uint32_t i) {
  x.s.configs[i].dead = false;
  x.s.configs[i].armNext = 0;
  lr_read_step<A>(c, x, i);
}

// Reading `start` and its forks read until each is refused, holds or accepts:
// those are `s.out`. One refused while an invented token is in hand is not:
// the table cannot take that token at all.
template <const auto& A>
void lr_search_read(LrContext& c, LrSearch& s, std::uint32_t start,
                    bool single = false) {
  std::vector<std::uint32_t>& out = s.out;
  out.clear();
  s.reading.clear();
  s.reading.push_back(start);
  LrSearchPool<A> x{c, s, single};
  for (std::size_t w = 0; w < s.reading.size(); ++w) {
    c.stop.poll();
    const std::uint32_t i = s.reading[w];
    for (;;) {
      const LrConfig& r = s.configs[i];
      if (r.gone) break;
      if (r.done || (!r.pin && lr_search_goal(r))) {
        out.push_back(i);
        break;
      }
      if (s.work > s.limit) break;
      lr_search_step<A>(c, x, i);
      const LrConfig& q = s.configs[i];
      if (q.dead) {
        if (!q.pin && !q.gone) out.push_back(i);
        break;
      }
    }
  }
  lr_search_pay(c, s);
}

// The author's tokens under frozen frame `f`, counted from the top frame down
// and only past `most` as a floor.
inline std::int64_t lr_search_src_tokens(const LrContext& c, LrSearch& s,
                                         std::uint32_t f, std::int64_t most) {
  if (s.srcTokens.size() != s.cactus.frozen_count())
    s.srcTokens.assign(s.cactus.frozen_count(), -1);
  if (s.srcTokens[f] >= 0) return s.srcTokens[f];
  const LrCactus::Frozen fz = s.cactus.frozen(f);
  LrSearch::Source& so = s.sources[fz.source];
  const std::uint32_t first = f - fz.frame;
  for (std::uint32_t j = so.frames->size(); j-- > fz.frame;) {
    if (s.srcTokens[first + j] >= 0) continue;
    const NodeCount open = (*so.frames)[j].open;
    for (; so.scanned > open; --so.scanned) {
      if (so.count > most) return so.count;
      ++s.work;
      if (so.scanned > c.tape.len()) {
        ++so.count;
        continue;
      }
      const CstNode& n = c.tape.at(so.scanned - 1);
      if (n.subtreeSize == 1 && !n.hidden && !n.recovered && n.begin < n.end)
        ++so.count;
    }
    s.srcTokens[first + j] = so.count;
  }
  return s.srcTokens[f];
}

inline std::uint32_t lr_search_child(LrSearch& s, std::uint32_t from,
                                     LrGesture::Kind kind, SymbolId term,
                                     std::uint32_t end,
                                     std::uint32_t choice) {
  s.configs.push_back(s.configs[from]);
  LrConfig& e = s.configs.back();
  e.link = s.link(LrGesture{.parent = e.link,
                            .choice = choice,
                            .kind = kind,
                            .opens = e.fresh,
                            .term = term,
                            .end = end,
                            .at = e.at,
                            .la = e.la,
                            .node = e.run.top(),
                            .low = e.run.low(),
                            .cost = e.cost});
  e.faults += e.fresh ? 1u : 0u;
  e.fresh = false;
  e.shifts = 0;
  e.dead = e.gone = e.weighed = e.queued = e.pinDue = false;
  ++s.work;
  return static_cast<std::uint32_t>(s.configs.size()) - 1;
}

template <const auto& A>
inline constexpr auto kLrOwedAnywhere = [] {
  std::remove_cvref_t<decltype(A.cold.follow[0])> all{};
  for (const auto& row : A.cold.closePool) all |= row;
  return all;
}();

// A terminal the state shifts outright is owed by the state's own row alone.
template <const auto& A>
inline bool lr_search_owed_maybe(std::uint32_t state, SymbolId t) {
  if (!kLrOwedAnywhere<A>.test(t)) return false;
  if (A.cold.close_row(static_cast<std::uint16_t>(state)).test(t)) return true;
  std::uint32_t first = 0;
  unsigned count = 0;
  lr_action_run<A>(state, t, first, count);
  return count != 1 || !lr_takes_token_in<A>(lr_shadow_action<A>(first).kind);
}

// In a section the token that closes it is owed only where the document ends
// inside it.
template <const auto& A>
inline bool lr_search_insertable(std::uint32_t state, SymbolId t, bool atEnd) {
  return t != kEofSymbol && A.cfg.terms[t].laid != kLaidImplied &&
         (atEnd || t != lr_text_close<A>(state));
}

// What the table does with an invented `term` over the stack ending at `top`:
// the reductions it makes on it, then refuses it, or shifts it. Where it forks
// on it, accepts it, or makes more reductions than `ch` holds, nothing is told.
enum class LrChain : std::uint8_t { open, refused, shifts };

template <const auto& A>
LrChain lr_search_chain(const LrCactus& cactus, std::uint32_t top,
                        SymbolId term, LrSearch::Chain& ch) {
  std::uint32_t node = top;
  std::uint32_t size = cactus.depth(top);
  unsigned above = 0;
  ch.n = ch.attempts = 0;
  for (;;) {
    const std::uint32_t state =
        above != 0 ? ch.above[above - 1] : cactus.state(node);
    std::uint32_t first = 0;
    unsigned count = 0;
    lr_action_run<A>(state, term, first, count);
    if (count == 0) return LrChain::refused;
    if (count != 1) return LrChain::open;
    const LrAction x = lr_shadow_action<A>(first);
    if (x.kind == LrActionKind::shift) {
      ch.shift = x.arg;
      return LrChain::shifts;
    }
    if (x.kind != LrActionKind::reduce || ch.n == LrSearch::Chain::kRoom)
      return LrChain::open;
    ++ch.attempts;
    const auto& prod = A.cfg.prods[x.arg];
    if (prod.len + 1u > size) return LrChain::refused;
    size -= prod.len;
    for (std::uint32_t k = prod.len; k != 0; --k) {
      if (above != 0)
        --above;
      else
        node = cactus.parent(node);
    }
    const std::uint32_t below =
        above != 0 ? ch.above[above - 1] : cactus.state(node);
    const std::uint32_t to = lr_goto_of<A>(below, prod.lhs);
    if (to == kInvalidSymbol) return LrChain::refused;
    ch.prod[ch.n] = x.arg;
    ch.to[ch.n++] = static_cast<std::uint16_t>(to);
    ch.above[above++] = static_cast<std::uint16_t>(to);
    ++size;
  }
}

// The reductions an invented token makes and the shift that takes it, made on
// its reading as its steps would make them, at the work they would be charged.
template <const auto& A>
void lr_search_forward(LrSearch& s, LrConfig& r, const LrSearch::Chain& ch) {
  const auto stand = [&] {
    const std::uint16_t mode = A.mode[r.run.back()];
    if (r.laMode != mode &&
        (r.laMode == kNoMode || !A.mode_sub(r.laMode, mode) ||
         lr_has_rivals<A>(mode, r.la.sym))) {
      r.la = r.pinTok;
      r.laMode = mode;
    }
    ++s.work;
  };
  for (std::uint32_t k = 0; k < ch.n; ++k) {
    stand();
    if (A.derived.reductions[ch.prod[k]].flushes && r.laMode != kNoMode &&
        r.la.begin > r.at)
      r.at = r.la.begin;
    r.run.pop(A.cfg.prods[ch.prod[k]].len);
    r.run.push(ch.to[k]);
  }
  stand();
  r.run.push(ch.shift);
  r.laMode = kNoMode;
  if (!r.pin || r.pinRep || !(r.la == r.pinTok)) r.at = r.la.end;
  r.template shifted<A>(A.cfg.terms[r.la.sym].laid != 0);
}

// Its readings go to `s.out`; an owed insertion that goes on a run of them
// costs nothing. A token the table refuses outright is paid for as reading it
// would have been, and lays nothing: `kNoGestureLink`.
template <const auto& A>
std::uint32_t lr_search_insert(LrContext& c, LrSearch& s, std::uint32_t bi,
                               SymbolId term, std::uint32_t choice, bool& due) {
  [[maybe_unused]] bool chained = false;
  if constexpr (!kLrConditioned<A> && !kIndented<A.cfg>) {
    const LrChain told =
        lr_search_chain<A>(s.cactus, s.configs[bi].run.top(), term, s.chain);
    if (told == LrChain::refused && s.work + 1 + s.chain.attempts <= s.limit) {
      s.work += 1 + s.chain.attempts;
      s.out.clear();
      due = false;
      return kNoGestureLink;
    }
    chained = told == LrChain::shifts && s.work + s.chain.n + 1 <= s.limit;
  }
  const std::uint32_t e =
      lr_search_child(s, bi, LrGesture::ins, term, 0, choice);
  LrConfig& r = s.configs[e];
  const std::uint64_t free = r.freeInserts;
  const std::uint64_t most = saturating_mul(r.run.size(), A.cold.closeMax);
  lr_pin(r, term, false, 0, Token{}, A.mode[r.run.back()]);
  r.invented += 1;
  r.phase = LrRepairPhase::inserting;
  const std::uint32_t link = r.link;
  if constexpr (!kLrConditioned<A> && !kIndented<A.cfg>)
    if (chained) lr_search_forward<A>(s, r, s.chain);
  lr_search_read<A>(c, s, e);
  due = false;
  for (const std::uint32_t q : s.out) {
    LrConfig& x = s.configs[q];
    const bool cont = x.pinDue && free != 0;
    x.cost += cont ? 0 : kRepairGesture;
    if (!x.fresh) {
      x.freeInserts =
          x.pinDue ? (cont ? free - 1 : (most > 0 ? most - 1 : 0)) : 0;
    }
    due = due || x.pinDue;
  }
  return link;
}

// The terminals a state offers a repair to insert, in the order the grammar
// wrote them -- those its list names first, then the rest by id -- each with
// its place in that order. `visit` answers whether to go on.
template <const auto& A, class Visit>
void lr_each_insert(std::uint32_t state, Visit&& visit) {
  auto want = A.cold.follow[state];
  unsigned rank = 0;
  if constexpr (A.cold.insertWord.size() != 0) {
    if (const std::uint32_t k = A.cold.insertOf[state]; k != 0)
      for (auto i = A.cold.insertAt[k - 1u]; i < A.cold.insertAt[k]; ++i) {
        const unsigned t = A.cold.insertWord[i];
        if (t >= A.cfg.nterms || !want.test(t)) continue;
        want.reset(t);
        if (!visit(t, rank++)) return;
      }
  }
  for (unsigned t = want.take(); t < A.cfg.nterms; t = want.take())
    if (!visit(t, rank++)) return;
}

template <const auto& A>
void lr_search_inserts(LrContext& c, LrSearch& s, std::uint32_t gi) {
  const auto from = s.configs[gi].source();
  const auto rank = s.configs[gi].insertRank();
  const auto state = s.configs[from].run.back();
  lr_each_insert<A>(state, [&](unsigned t, unsigned k) {
    if (lr_search_spent(s)) return false;
    const auto term = static_cast<SymbolId>(t);
    if (!lr_search_insertable<A>(state, term,
                                 s.configs[from].la.begin >= c.len) ||
        lr_search_owed_maybe<A>(state, term))
      return true;
    bool due = false;
    const LrSearchLaid pools(s);
    lr_search_insert<A>(c, s, from, term, rank + k, due);
    lr_search_admit_out(s, pools);
    return true;
  });
}

// The token in hand as the lexer's refused reading of it. Closed, one
// re-reading; stopped by its own line's end, every end that cuts no word, the
// shortest first; open further, its line ends in order up to the first a goal
// holds after, then the end of the text -- past a line the text resumes at, a
// token that never closed is not read. A trivia so read is skipped, the
// author's bytes kept. The ends are found once per search, and only as far as
// the share lets the readings they open be read.
template <const auto& A>
void lr_search_lexical(LrContext& c, LrSearch& s, std::uint32_t bi,
                       const Token& written, std::uint32_t& choice) {
  const LrConfig& b = s.configs[bi];
  const std::uint32_t state = b.run.back();
  const std::uint16_t mode = A.mode[state];
  const TextOffset p = b.la.begin;
  const LexReading r = lr_search_reading<A>(c, s, mode, p);
  if (r.term == kInvalidSymbol || r.term == lr_text_close<A>(state)) return;
  const bool trivia = lr_is_trivia<A>(r.term);
  if (!trivia && (!b.mayReplace() || !A.cold.follow[state].test(r.term)))
    return;
  const auto one = [&](TextOffset end) {
    const LrSearchLaid pools(s);
    const std::uint32_t e =
        lr_search_child(s, bi, trivia ? LrGesture::skip : LrGesture::rep,
                        r.term, end, choice++);
    LrConfig& x = s.configs[e];
    x.cost += kRepairReread;
    x.freeInserts = 0;
    if (trivia) {
      x.at = end;
      x.laMode = kNoMode;
      x.phase = LrRepairPhase::discardOnly;
    } else {
      lr_pin(x, r.term, true, end, written, mode);
    }
    lr_search_read<A>(c, s, e);
    const bool holds = std::ranges::any_of(
        s.out, [&](std::uint32_t q) { return lr_search_goal(s.configs[q]); });
    lr_search_admit_out(s, pools);
    return holds;
  };
  if (r.closed) {
    one(r.end);
    return;
  }
  RepairEnds& ends = s.ends[(std::uint64_t{p} << 16) | mode];
  const auto cuts = [&](TextOffset x) {
    return lex_cuts<A.cfg>(c.src, c.len, x);
  };
  for (std::size_t k = 0; !lr_search_spent(s); ++k) {
    if (k == ends.found.size() && !ends.more(c.src, c.len, p, r, s.work, cuts))
      return;
    if (one(ends.found[k]) && ends.regime == RepairEnds::lines) return;
  }
}

// Whether refused reading `b` may go on by a re-reading of the token in hand;
// a walk its share cut may have found one.
template <const auto& A>
bool lr_search_rereads(LrContext& c, LrSearch& s, const LrConfig& b) {
  const bool laid = b.la.sym < A.cfg.nterms && A.cfg.terms[b.la.sym].laid != 0;
  if (b.la.begin >= c.len || laid) return false;
  const std::uint32_t state = b.run.back();
  if (lr_search_reading<A>(c, s, A.mode[state], b.la.begin).term !=
      kInvalidSymbol)
    return true;
  return (b.mayReplace() &&
          !lr_search_typos<A>(c, s, state, b.la.begin).empty()) ||
         lr_search_spent(s);
}

// The gestures the canonical form admits, in rank order: re-readings, owed
// insertions, the others, the drop, and the abandons one depth at a time.
template <const auto& A>
void lr_search_expand(LrContext& c, LrSearch& s, std::uint32_t bi) {
  const LrConfig b = s.configs[bi];
  const std::uint32_t state = b.run.back();
  const std::uint16_t mode = A.mode[state];
  const bool atEnd = b.la.begin >= c.len;
  const bool laid = b.la.sym < A.cfg.nterms && A.cfg.terms[b.la.sym].laid != 0;
  std::uint32_t choice = 0;
  // The generator pays the fault it opens here: queued, it is not counted
  // again.
  const auto defer = [&](LrConfig g) {
    g.cost += kRepairGesture;
    g.faults += b.fresh ? 1u : 0u;
    s.configs.push_back(g);
    lr_search_queue(s, g.cost, g,
                    static_cast<std::uint32_t>(s.configs.size()) - 1);
  };
  const Token written = lr_search_written<A>(c, s, b.la, mode);
  if (!atEnd && !laid) lr_search_lexical<A>(c, s, bi, written, choice);
  if (b.mayReplace() && !atEnd && !laid) {
    for (const Typo& t : lr_search_typos<A>(c, s, state, b.la.begin)) {
      if (lr_search_spent(s)) break;
      if (t.term == lr_text_close<A>(state)) continue;
      ++s.work;
      const LrSearchLaid pools(s);
      const std::uint32_t e =
          lr_search_child(s, bi, LrGesture::rep, t.term, t.end, choice++);
      LrConfig& r = s.configs[e];
      lr_pin(r, t.term, true, t.end, written, mode);
      r.cost += kRepairReread;
      r.freeInserts = 0;
      lr_search_read<A>(c, s, e);
      lr_search_admit_out(s, pools);
    }
  }
  // Bytes no mode reads refuse every reading alike: insertions in front of
  // them are told apart only past them, so one is made there, never a run.
  const bool garbage =
      !atEnd && !laid && written.sym == kInvalidSymbol &&
      lr_search_reading<A>(c, s, mode, b.la.begin).term == kInvalidSymbol;
  if (b.mayInsert(garbage)) {
    // Only an owed run costs nothing past its first, so the terminals that
    // cannot be owed wait behind one entry until a reading that dear is asked
    // for.
    const std::uint32_t insertRank = choice;
    bool deferred = false;
    lr_each_insert<A>(state, [&](unsigned t, unsigned k) {
      const auto term = static_cast<SymbolId>(t);
      if (!lr_search_insertable<A>(state, term, atEnd)) return true;
      if (lr_search_spent(s)) return false;
      if (!lr_search_owed_maybe<A>(state, term)) {
        deferred = true;
        return true;
      }
      bool due = false;
      const LrSearchLaid pools(s);
      const std::uint32_t link =
          lr_search_insert<A>(c, s, bi, term, insertRank + k, due);
      if (!due && link != kNoGestureLink) s.links[link].choice += A.cfg.nterms;
      lr_search_admit_out(s, pools);
      return true;
    });
    // Ranks need order, not consecutive values: two terminal-sized ranges
    // put owed insertions first, then all others in declaration order.
    choice += 2u * A.cfg.nterms;
    if (deferred) {
      LrConfig g = LrConfig::insertGenerator(b, bi, insertRank + A.cfg.nterms);
      g.invented += 1;
      defer(g);
    }
  }
  if (!atEnd && written.sym != lr_text_close<A>(state)) {
    const TextOffset end = lr_search_error_end<A>(c, s, mode, b.la);
    const LrSearchLaid pools(s);
    const std::uint32_t e =
        lr_search_child(s, bi, LrGesture::del, 0, end, choice++);
    LrConfig& r = s.configs[e];
    lr_drop_levels<A>(c, r.virt, r.indent, b.la);
    if (end > r.at) r.at = end;
    if constexpr (kLrConditioned<A>)
      if (end != b.la.begin) r.virt.from = end;
    r.laMode = kNoMode;
    r.cost += kRepairGesture;
    r.lost += laid ? 0u : 1u;
    r.phase = LrRepairPhase::discardOnly;
    r.freeInserts = 0;
    lr_search_read<A>(c, s, e);
    lr_search_admit_out(s, pools);
  }
  if (b.mayAbandon() && b.run.size() > 1) {
    LrConfig g = LrConfig::abandonGenerator(
        b, bi, b.run.size() - 1, b.run.top(), b.run.begin_node(), choice);
    g.lost += 1;
    defer(g);
  }
}

// The shallowest abandon left that gives up text and whose state takes the
// token in hand: laid at the cost it was queued at, else queued at its own.
template <const auto& A>
void lr_search_abandon(LrContext& c, LrSearch& s, std::uint32_t gi,
                       std::uint32_t f) {
  LrConfig g = s.configs[gi];
  const std::int64_t tokens = s.configs[g.source()].run.tokens();
  const SymbolId la = s.configs[g.source()].la.sym;
  const std::uint32_t baseCost = s.configs[g.source()].cost;
  const std::uint32_t baseLost = s.configs[g.source()].lost;
  std::uint32_t node = g.abandonNode();
  std::uint32_t begin = g.abandonBegin();
  const auto requeue = [&](std::uint32_t keep, std::uint32_t at,
                           std::uint32_t from, std::uint32_t cost,
                           std::uint32_t lost) {
    LrConfig& q = s.configs[gi];
    q.resumeAbandons(keep, at, from, g.armNext);
    q.cost = cost;
    q.lost = lost;
    lr_search_queue(s, cost, q, gi);
  };
  for (std::uint32_t keep = g.abandonDepth(); keep >= 1;
       --keep, node = s.cactus.parent(node),
                     begin = s.cactus.begin_parent(begin)) {
    if (lr_search_spent(s)) return;
    ++s.work;
    const std::uint32_t below = s.cactus.parent(node);
    if (la >= A.cfg.nterms ||
        !A.cold.follow[s.cactus.state(below)].test(la))
      continue;
    const std::int64_t at = s.cactus.begin_of(begin);
    // Counted only as far as this cost could pay: past it, a floor.
    const std::int64_t lost =
        at >= 0 ? tokens - at
                : tokens + lr_search_src_tokens(
                               c, s, static_cast<std::uint32_t>(-1 - at),
                               std::max<std::int64_t>(
                                   (std::int64_t{f} - baseCost) / kRepairGesture -
                                       tokens,
                                   0));
    if (lost < 1) continue;
    const auto cost = static_cast<std::uint32_t>(baseCost + lost * kRepairGesture);
    const auto lostAll = static_cast<std::uint32_t>(baseLost + lost);
    if (cost > f) {
      requeue(keep, node, begin, cost, lostAll);
      return;
    }
    const LrSearchLaid pools(s);
    const std::uint32_t e = lr_search_child(s, g.source(), LrGesture::abandon,
                                            0, keep, g.armNext++);
    LrConfig& r = s.configs[e];
    r.run.truncate(r.run.size() - keep);
    r.cost = cost;
    r.lost = lostAll;
    r.phase = LrRepairPhase::afterAbandon;
    r.freeInserts = 0;
    lr_search_read<A>(c, s, e);
    lr_search_admit_out(s, pools);
    if (keep > 1)
      requeue(keep - 1, s.cactus.parent(node), s.cactus.begin_parent(begin),
              cost, lostAll);
    return;
  }
}

// The goals read on with no gesture: `reach[k]` is where candidate `k`'s
// furthest reading was refused, or `kRepairReachAll`. The reading furthest back
// steps, so two bound for one future meet and go on as one; settled once
// every reading still going stands for the same candidates past every other's
// refusal. True where the search's share ran out first.
template <const auto& A>
bool lr_search_race(LrContext& c, LrSearch& s,
                    const std::vector<std::uint32_t>& of,
                    std::vector<TextOffset>& reach) {
  const std::vector<std::uint32_t>& goals = s.goals;
  struct Runner {
    std::uint32_t cfg;
    std::vector<std::uint32_t> of;
  };
  const std::uint32_t n = goals.empty() ? 0 : *std::ranges::max_element(of) + 1;
  reach.assign(n, 0);
  std::vector<Runner> runners;
  std::vector<std::uint32_t> queue;
  const auto behind = [&](std::uint32_t a, std::uint32_t b) {
    return s.configs[runners[a].cfg].at > s.configs[runners[b].cfg].at;
  };
  for (std::uint32_t k = 0; k < goals.size(); ++k) {
    if (s.configs[goals[k]].done) {
      reach[of[k]] = kRepairReachAll;
      continue;
    }
    const LrConfig g = s.configs[goals[k]];
    s.configs.push_back(g);
    runners.push_back(
        Runner{static_cast<std::uint32_t>(s.configs.size()) - 1, {of[k]}});
    queue.push_back(static_cast<std::uint32_t>(runners.size()) - 1);
    std::ranges::push_heap(queue, behind);
  }
  const auto settle = [&](const std::vector<std::uint32_t>& ks) {
    for (const std::uint32_t k : ks) reach[k] = kRepairReachAll;
  };
  const auto level = [&] {
    const std::vector<std::uint32_t>& first = runners[queue[0]].of;
    TextOffset least = kRepairReachAll;
    for (const std::uint32_t q : queue) {
      if (runners[q].of != first) return false;
      least = std::min(least, s.configs[runners[q].cfg].at);
    }
    for (std::uint32_t k = 0; k < n; ++k)
      if (!std::ranges::binary_search(first, k) && reach[k] >= least)
        return false;
    return true;
  };
  LrSearch::Futures met(0, s.seen.hash_function(), s.seen.key_eq());
  // Only at a token boundary can a reading join the one waiting on its future.
  const auto place = [&](std::uint32_t w, std::int64_t at) {
    const LrConfig& q = s.configs[runners[w].cfg];
    if (q.gone || q.dead) {
      for (const std::uint32_t k : runners[w].of)
        reach[k] = std::max(reach[k], q.la.begin);
      return;
    }
    if (q.done) {
      settle(runners[w].of);
      return;
    }
    if (q.run.tokens() != at) {
      const auto [it, fresh] = met.try_emplace(LrSearch::Future::of(q), w);
      if (!fresh) {
        std::vector<std::uint32_t> both;
        std::ranges::set_union(runners[it->second].of, runners[w].of,
                               std::back_inserter(both));
        runners[it->second].of = std::move(both);
        return;
      }
    }
    queue.push_back(w);
    std::ranges::push_heap(queue, behind);
  };
  LrSearchPool<A> x{c, s};
  while (!queue.empty()) {
    c.stop.poll();
    const bool settled = level();
    if (settled || s.work > s.limit) {
      for (const std::uint32_t q : queue) settle(runners[q].of);
      return !settled;
    }
    std::ranges::pop_heap(queue, behind);
    const std::uint32_t w = queue.back();
    queue.pop_back();
    const std::uint32_t i = runners[w].cfg;
    if (const auto it = met.find(LrSearch::Future::of(s.configs[i]));
        it != met.end() && it->second == w)
      met.erase(it);
    const std::int64_t at = s.configs[i].run.tokens();
    s.reading.clear();
    lr_search_step<A>(c, x, i);
    lr_search_pay(c, s);
    for (const std::uint32_t j : s.reading) {
      runners.push_back(Runner{j, runners[w].of});
      place(static_cast<std::uint32_t>(runners.size()) - 1, at);
    }
    place(w, at);
  }
  return false;
}

// An arm the drive takes on its own is no gesture; one taken while an invented
// token is in hand is the writer's, and so the path's.
inline bool lr_same_gestures(const std::vector<LrGesture>& links,
                             std::uint32_t a, std::uint32_t b) {
  const auto drives = [&](std::uint32_t l) {
    return l != kNoGestureLink && links[l].kind == LrGesture::arm &&
           !links[l].held && links[l].parent != kNoGestureLink;
  };
  for (;;) {
    while (drives(a)) a = links[a].parent;
    while (drives(b)) b = links[b].parent;
    if (a == b) return true;
    if (a == kNoGestureLink || b == kNoGestureLink) return false;
    const LrGesture& x = links[a];
    const LrGesture& y = links[b];
    if (x.parent == kNoGestureLink || y.parent == kNoGestureLink || x.kind != y.kind ||
        x.term != y.term || x.end != y.end || x.at != y.at || !(x.la == y.la) ||
        (x.kind == LrGesture::arm && x.choice != y.choice))
      return false;
    a = x.parent;
    b = y.parent;
  }
}

// Two paths that make the same gestures fold alike.
inline std::uint64_t lr_gestures_fold(const std::vector<LrGesture>& links,
                                      std::uint32_t l) {
  std::uint64_t h = 0;
  for (; links[l].parent != kNoGestureLink; l = links[l].parent) {
    const LrGesture& g = links[l];
    if (g.kind == LrGesture::arm && !g.held) continue;
    const std::uint64_t v =
        (std::uint64_t{g.kind} << 56) ^ (std::uint64_t{g.term} << 40) ^
        (std::uint64_t{g.la.sym} << 24) ^ (std::uint64_t{g.end} << 20) ^
        g.at ^ (g.kind == LrGesture::arm ? std::uint64_t{g.choice} << 48 : 0);
    h = (h ^ v) * 0x100000001b3ull;
  }
  return (h ^ l) * 0x100000001b3ull;
}

// A plan names nodes of the cactus it was elected on: a search that lays
// another drops it.
inline void lr_search_reset(LrSearch& s) {
  const auto clear = [](auto&... x) { (x.clear(), ...); };
  s.restart();
  s.cactus.reset();
  clear(s.sources, s.configs, s.seen, s.srcTokens, s.lexed, s.readings, s.ends,
        s.typos, s.written, s.ahead, s.misread, s.plan, s.planArms);
  s.errorEnds.reset();
  s.owes.reset();
  s.unread.reset();
  s.next = 0;
  s.horizon = 0;
}

// A root is refused, so it owes at least one gesture.
template <class Reading>
void lr_search_root(LrSearch& s, const Reading& v, TextOffset at,
                    const LrStack& frames, NodeCount len, std::uint32_t choice,
                    bool fresh) {
  const auto src = static_cast<std::uint32_t>(s.sources.size());
  const LrCactus::Top top = s.cactus.add(frames, src);
  LrConfig root;
  root.run = LrCactusClimb(s.cactus, top);
  root.at = at;
  root.la = v.la;
  root.laMode = v.laMode;
  root.virt = v.virt;
  root.indent = v.indent;
  root.link = s.link(LrGesture{.choice = choice});
  root.fresh = fresh;
  s.sources.push_back(LrSearch::Source{.frames = &frames, .scanned = len});
  s.horizon = std::max(s.horizon, v.la.begin + kLrBoundReach);
  s.configs.push_back(root);
  lr_search_queue(s, kRepairReread, root,
                  static_cast<std::uint32_t>(s.configs.size()) - 1);
}

inline void lr_search_start(const LrContext& c, LrSearch& s) {
  lr_search_reset(s);
  s.paidBase = c.cost;
  // A fault a stopped search left open goes on.
  lr_search_root(s, c, c.committed, c.stack, c.tape.len(), 0, !s.fault.open);
}

// With no goal before the work ran out, the reading that got furthest.
template <const auto& A>
std::uint32_t lr_search_run(LrContext& c, LrSearch& s, bool& exhausted,
                            bool& raced) {
  s.gapsPaid = c.gaps.read();
  std::uint32_t level = 0;
  std::uint64_t levelWork = s.work;
  bool bounds = false;
  constexpr std::uint32_t kNone = kLrNoConfig;
  std::uint32_t furthest = kNone;
  exhausted = false;
  raced = false;
  while (!s.heap.empty()) {
    c.stop.poll();
    const RepairEntry e = s.pop();
    if (s.configs[e.at].gone) continue;
    if (!s.goals.empty() && e.f > s.configs[s.goals[0]].cost) break;
    if (s.work > s.limit) {
      exhausted = true;
      break;
    }
    switch (s.configs[e.at].work) {
      case LrWorkKind::read:
        break;
      case LrWorkKind::enumerateInsertions:
        lr_search_inserts<A>(c, s, e.at);
        continue;
      case LrWorkKind::enumerateAbandons:
        lr_search_abandon<A>(c, s, e.at, e.f);
        continue;
    }
    const LrConfig& x = s.configs[e.at];
    if (lr_search_goal(x)) {
      s.goals.push_back(e.at);
      continue;
    }
    if (furthest == kNone || x.at > s.configs[furthest].at) furthest = e.at;
    // Queued as if a re-reading went on from it: where none does, it owes a
    // whole gesture.
    if (x.freeInserts == 0 && e.f < x.cost + kRepairGesture &&
        !lr_search_rereads<A>(c, s, x)) {
      lr_search_queue(s, x.cost + kRepairGesture, x, e.at, e.lost - x.lost);
      continue;
    }
    // Past `boundAfter` of work at one cost, each reading is bounded once and
    // waits if it owes more than it was queued at.
    if (e.f != level) {
      level = e.f;
      levelWork = s.work;
    }
    bounds = bounds || s.work - levelWork >= s.boundAfter;
    if (bounds && !x.weighed) {
      s.configs[e.at].weighed = true;
      const std::uint32_t owed = lr_search_bound<A>(c, s, x);
      const std::uint32_t f = x.cost + owed / kLrPaid;
      const std::uint32_t lost = owed % kLrPaid;
      if (f > e.f || (f == e.f && x.lost + lost > e.lost)) {
        lr_search_queue(s, f, s.configs[e.at], e.at, lost);
        continue;
      }
    }
    lr_search_expand<A>(c, s, e.at);
  }
  // A walk the share cut leaves the goals of its cost unread: the bound
  // decided, even where the bucket emptied.
  exhausted = exhausted || s.work > s.limit;
  if (s.goals.empty()) return furthest;
  return repair_elect<lr_gestures_fold, lr_same_gestures, lr_search_kind>(
      s, s.configs, true, [&](const auto& of, auto& reach) {
        raced = lr_search_race<A>(c, s, of, reach);
      });
}

// `open`: a path elected short of a goal stops inside its last fault, for the
// next search to go on with.
inline void lr_search_plan(const LrContext& c, LrSearch& s, std::uint32_t ci,
                           bool open) {
  std::vector<std::uint32_t> path;
  for (std::uint32_t l = s.configs[ci].link;
       l != kNoGestureLink && s.links[l].parent != kNoGestureLink; l = s.links[l].parent) {
    path.push_back(l);
  }
  s.plan.clear();
  s.planArms.clear();
  s.next = 0;
  for (std::size_t k = path.size(); k-- > 0;) {
    const LrGesture& g = s.links[path[k]];
    if (g.kind != LrGesture::arm) {
      LrSearch::Step st;
      st.g = g;
      st.armBase = static_cast<std::uint32_t>(s.planArms.size());
      s.plan.push_back(st);
    } else if (g.held && !s.plan.empty()) {
      s.planArms.push_back(g.choice);
      ++s.plan.back().armCount;
    }
  }
  for (std::size_t k = 0; k < s.plan.size(); ++k) {
    LrSearch::Step& st = s.plan[k];
    st.closes = k + 1 == s.plan.size() || s.plan[k + 1].g.opens;
    st.before = s.paidBase + st.g.cost;
    st.paid = (k + 1 < s.plan.size() ? s.plan[k + 1].g.cost
                                     : s.configs[ci].cost) -
              st.g.cost;
    st.kept = c.stack[std::max(st.g.low, 1u) - 1];
  }
  if (open && !s.plan.empty() && !lr_search_goal(s.configs[ci]))
    s.plan.back().closes = false;
  if (!s.plan.empty() && s.plan[0].g.kind == LrGesture::ins)
    std::erase(s.also, static_cast<ElementId>(s.plan[0].g.term));
  else
    s.also.clear();
}

// The pause a step was made at: the token in hand, what was paid, and the
// stack above the frames its reading never moved -- read only as deep as it
// went, so a run of steps costs what they read.
inline bool lr_search_holds(const LrContext& c, const LrSearch& s,
                            const LrSearch::Step& st) {
  if (c.laMode == kNoMode || !(c.la == st.g.la) || c.cost != st.before)
    return false;
  std::uint32_t n = st.g.node;
  const std::uint32_t depth = s.cactus.depth(n);
  if (c.stack.size() != depth) return false;
  const std::uint32_t low = std::max(st.g.low, 1u);
  for (std::uint32_t d = depth; d-- > low;) {
    if (c.stack[d].state != s.cactus.state(n)) return false;
    n = s.cactus.parent(n);
  }
  const LrFrame& kept = c.stack[low - 1];
  return kept.state == st.kept.state && kept.open == st.kept.open &&
         kept.begin == st.kept.begin;
}

inline void lr_search_note(LrContext& c, LrSearch& s) {
  RepairFault& f = s.fault;
  if (!f.open) return;
  RepairNote n = repair_note(f);
  assert((c.notes.empty() || c.notes.back().end <= n.end) &&
         "a reading never lays a note behind the last one");
  if (s.bounded && n.begin < s.boundedBegin) s.boundedBegin = n.begin;
  c.notes.push_back(std::move(n));
  f = RepairFault{};
}

// What a construct abandoned in `state` owed: nothing named where its row is
// empty.
template <const auto& A>
ElementId lr_search_owed(std::uint32_t state) {
  const auto& row = A.cold.close_row(static_cast<std::uint16_t>(state));
  const unsigned t = row.first();
  return t < A.cfg.nterms ? static_cast<ElementId>(t) : kNoElement;
}

// False where the table refuses what the search read.
template <const auto& A>
bool lr_search_write(LrContext& c, LrSearch& s, const LrSearch::Step& st,
                     bool first) {
  RepairFault& f = s.fault;
  if (st.g.opens) lr_search_note(c, s);
  if (!f.open) {
    f.open = true;
    f.point = c.committed;
    f.begin = c.la.begin;
    f.end = c.la.begin;
  }
  const std::uint32_t state = c.stack.top().state;
  switch (st.g.kind) {
    case LrGesture::ins:
      if (!lr_write_token<A>(c, st.g.term, false, 0, Token{},
                             s.planArms.data() + st.armBase, st.armCount))
        return false;
      if (!f.ins) {
        f.ins = true;
        f.first = static_cast<ElementId>(st.g.term);
        if (first) f.also = s.also;
      }
      break;
    case LrGesture::rep: {
      Token written = c.la;
      // Only conditions can inspect the original token during replay.
      if constexpr (kLrConditioned<A>)
        written = lr_written<A>(c, c.la, A.mode[state]);
      const TextOffset begin = c.la.begin;
      if (!lr_write_token<A>(c, st.g.term, true, st.g.end, written,
                             s.planArms.data() + st.armBase, st.armCount))
        return false;
      f.rep = true;
      f.read = static_cast<ElementId>(st.g.term);
      if (!f.del && !f.abandon) f.begin = begin;
      f.end = st.g.end;
      break;
    }
    case LrGesture::skip:
      if (!f.del && !f.abandon) f.begin = c.la.begin;
      f.rep = true;
      f.read = static_cast<ElementId>(st.g.term);
      lr_write_skip<A>(c, st.g.term, st.g.end);
      f.end = st.g.end;
      break;
    case LrGesture::del: {
      const TextOffset end = lr_error_end<A>(c, A.mode[state], c.la);
      if (!f.del && !f.abandon) f.begin = c.la.begin;
      f.del = true;
      lr_write_delete<A>(c, end);
      f.end = c.committed;
      // What the reading was skipped to.
      f.resume = static_cast<ElementId>(
          lr_scan_at<A>(c, A.mode[c.stack.top().state], c.committed,
                        c.indent.depth)
              .sym);
      break;
    }
    case LrGesture::abandon: {
      f.owed = lr_search_owed<A>(state);
      f.abandon = true;
      f.begin = st.g.end < c.stack.size() ? c.stack[st.g.end].begin
                                          : c.committed;
      lr_write_abandon<A>(c, st.g.end, kNoRow);
      f.end = c.committed;
      break;
    }
    case LrGesture::arm:
      break;
  }
  c.cost += st.paid;
  s.wroteShifts = c.shifts;
  if (st.closes) lr_search_note(c, s);
  return true;
}

// Said only on a reading that one the bound decided could have outranked:
// below them all the span waits, and dies with the parse.
inline void lr_search_bounded(LrContext& c, LrSearch& s) {
  if (!s.bounded || c.cost < s.boundedLeast) return;
  s.bounded = false;
  s.boundedLeast = ~std::uint64_t{0};
  RepairNote n{};
  n.kind = DiagnosticKind::bounded;
  n.begin = s.boundedBegin;
  n.end = s.boundedEnd > s.boundedBegin ? s.boundedEnd : s.boundedBegin;
  if (!c.notes.empty() && c.notes.back().end > n.end)
    n.end = c.notes.back().end;
  c.notes.push_back(n);
}

// A span a bound decided is said once the whole path is written.
inline LrExit lr_search_wrote(LrContext& c, LrSearch& s) {
  const bool end = s.next == s.plan.size();
  if (end && s.fault.open) s.openAt = c.committed;
  if (s.degraded) s.degradedShifts = c.shifts;
  if (s.bounded) {
    s.boundedEnd = c.committed;
    if (end && !s.fault.open && !s.degraded) lr_search_bounded(c, s);
  }
  return lr_resume(c);
}

// At the end of the text, the owed insertions one reading makes in a row,
// each the first the state's row offers that its own shift owes -- decided on
// the arm the writer takes, the first wherever the table forks -- until the
// reading accepts, owes nothing, or the run reaches its bound. The reading is
// `configs[0]`, moved on from step to step: an attempt that is not its next
// step leaves nothing behind, and what stands behind it is the plan's links.
// Whether it made any.
template <const auto& A>
bool lr_search_closers(LrContext& c, LrSearch& s) {
  lr_search_start(c, s);
  s.limit = s.degradedLimit;
  s.gapsPaid = c.gaps.read();
  bool closed = false;
  while (!s.configs[0].done) {
    c.stop.poll();
    if (s.degradedRun >= s.degradedCap || s.work > s.limit) {
      ++c.stats.degradedStops;
      break;
    }
    std::uint32_t took = kLrNoConfig;
    lr_each_insert<A>(s.configs[0].run.back(), [&](unsigned t, unsigned) {
      if (took != kLrNoConfig) return false;
      const auto term = static_cast<SymbolId>(t);
      if (!lr_search_insertable<A>(s.configs[0].run.back(), term,
                                   s.configs[0].la.begin >= c.len) ||
          !lr_search_owed_maybe<A>(s.configs[0].run.back(), term))
        return true;
      const LrSearchLaid pools(s);
      const std::uint32_t e = lr_search_child(s, 0, LrGesture::ins, term, 0, 0);
      LrConfig& r = s.configs[e];
      lr_pin(r, term, false, 0, Token{}, A.mode[r.run.back()]);
      r.cost += kRepairGesture;
      r.invented += 1;
      r.phase = LrRepairPhase::inserting;
      // The reading itself goes on as the first arm at every fork: the
      // writer's.
      lr_search_read<A>(c, s, e, true);
      c.stats.degradedHeld = std::max<std::uint32_t>(
          c.stats.degradedHeld, static_cast<std::uint32_t>(s.configs.size()));
      if (!s.configs[e].pin && s.configs[e].pinDue)
        took = e;
      else
        lr_search_trim(s, pools);
      return true;
    });
    if (took == kLrNoConfig) break;
    s.configs[0] = s.configs[took];
    s.configs.erase(s.configs.begin() + 1, s.configs.end());
    ++s.degradedRun;
    closed = true;
  }
  return closed;
}

// The first gesture that holds: the token dropped, or at the end the owed
// insertions, else a wrap. Each widens the span a bound decided.
template <const auto& A>
LrExit lr_search_degraded(LrContext& c, LrSearch& s) {
  if (s.degraded) ++c.stats.degradedPauses;
  // No token read since the last of them: the same fault goes on.
  if (c.shifts != s.degradedShifts) lr_search_note(c, s);
  // The budget decides this repair, whichever search spent it.
  lr_search_open(s, c.la.begin, c.cost + kRepairReread);
  const std::uint32_t state = c.stack.top().state;
  if (c.la.begin < c.len) {
    LrSearch::Step st;
    st.paid = kRepairGesture;
    st.g.kind = LrGesture::del;
    lr_search_write<A>(c, s, st, false);
    s.degradedShifts = c.shifts;
    s.boundedEnd = c.committed;
    return lr_resume(c);
  }
  if (s.degradedAt != c.committed) {
    s.degradedAt = c.committed;
    s.degradedRun = 0;
    // What one search may hold: the completion a grammar owes can be as long
    // as its rules say, and the plan holds a step for each insertion.
    s.degradedCap = std::min(
        saturating_mul(c.stack.size(), saturating_add(A.cold.closeMax, 1)),
        kRepairPerSearch);
    s.degradedLimit = saturating_add(s.work, s.perSearch);
  }
  // The first written, the rest planned: each pause after it takes its step.
  if (lr_search_closers<A>(c, s)) {
    lr_search_plan(c, s, 0, true);
    if (lr_search_write<A>(c, s, s.plan[s.next++], false))
      return lr_search_wrote(c, s);
    s.plan.clear();
    s.next = 0;
  }
  RepairFault& f = s.fault;
  f.open = true;
  f.abandon = true;
  f.owed = lr_search_owed<A>(state);
  f.begin = c.stack.size() > 1 ? c.stack[1].begin : c.committed;
  lr_write_abandon<A>(c, 1, A.cfg.entryRow);
  c.cost += kRepairGesture;
  f.end = c.committed;
  lr_search_note(c, s);
  s.boundedEnd = c.committed;
  lr_flush_trivia<A>(c);
  return LrExit::accept;
}

// A pause the reading goes on from without a search, `e` where it stops
// next: a dedent the end hands over for a level whose indent a repair dropped
// is a blank, and the plan's next step is taken where it was made. A pause
// it was not made at drops the plan.
template <const auto& A>
bool lr_search_goes_on(LrContext& c, LrSearch& s, LrExit& e) {
  if constexpr (kLaid<A.cfg, kLaidDedent> != 0) {
    if (c.la.sym == kLaid<A.cfg, kLaidDedent> && c.indent.head != kNoIndent &&
        c.indentArena[c.indent.head].mute) {
      c.indent.head = indent_unmute(c.indentArena, c.indent.head);
      c.laMode = kNoMode;
      e = lr_resume(c);
      return true;
    }
  }
  if (s.next >= s.plan.size()) return false;
  if (lr_search_holds(c, s, s.plan[s.next])) {
    const LrSearch::Step& st = s.plan[s.next++];
    if (lr_search_write<A>(c, s, st, false)) {
      if (s.degraded) ++c.stats.degradedPauses;
      e = lr_search_wrote(c, s);
      return true;
    }
  }
  ++c.stats.planDrops;
  s.plan.clear();
  s.next = 0;
  return false;
}

// The first step written, the rest planned; where there is none, or the table
// refuses it, the first gesture that holds.
template <const auto& A>
LrExit lr_search_take(LrContext& c, LrSearch& s, std::uint32_t elected,
                      bool exhausted, bool raced, bool open) {
  const TextOffset from = c.la.begin;
  if (exhausted)
    ++c.stats.budgetStops;
  else if (raced)
    ++c.stats.reachStops;
  if (exhausted || raced) lr_search_open(s, from, s.paidBase + kRepairReread);
  // Only the parse's budget leaves the pauses after it unsearched, whatever
  // stopped the search that spent it.
  s.degraded = s.work > s.budget;
  if (elected != kLrNoConfig)
    lr_search_plan(c, s, elected, open);
  else
    s.plan.clear();
  if (!s.plan.empty()) {
    if (lr_search_write<A>(c, s, s.plan[s.next++], true))
      return lr_search_wrote(c, s);
    s.plan.clear();
    s.next = 0;
    s.degraded = true;
  }
  lr_search_open(s, from, s.paidBase + kRepairReread);
  return lr_search_degraded<A>(c, s);
}

template <const auto& A>
LrExit lr_search_pause(LrContext& c) {
  c.stop.check();
  LrSearch& s = *c.search;
  if (LrExit e{}; lr_search_goes_on<A>(c, s, e)) return e;
  if (s.degraded) return lr_search_degraded<A>(c, s);
  if (s.openAt != c.committed) {
    lr_search_note(c, s);
    lr_search_bounded(c, s);
  }
  s.openAt = kLrNoOpen;
  bool exhausted = false, raced = false;
  lr_search_start(c, s);
  const std::uint32_t elected = lr_search_run<A>(c, s, exhausted, raced);
  return lr_search_take<A>(c, s, elected, exhausted, raced, true);
}

inline void lr_search_finish(LrContext& c, LrSearch& s) {
  lr_search_note(c, s);
  lr_search_bounded(c, s);
  c.stats.work = s.work;
  c.stats.searchRoom = static_cast<std::uint32_t>(s.configs.capacity());
}

}  // namespace cursiv
