// The repair search: best first over the edits a refused reading can make,
// each read ahead by the cells' own step until three tokens hold it. The
// cheapest that reads furthest wins, written one edit per pause.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/lr/runtime/LrCactus.hpp"
#include "cursiv/lr/runtime/LrCactusView.hpp"
#include "cursiv/lr/runtime/LrConfig.hpp"
#include "cursiv/lr/runtime/LrContext.hpp"
#include "cursiv/lr/runtime/LrDrive.hpp"
#include "cursiv/lr/runtime/LrEdit.hpp"
#include "cursiv/lr/runtime/LrExit.hpp"
#include "cursiv/lr/runtime/LrSearch.hpp"
#include "cursiv/lr/runtime/lr_read.hpp"
#include "cursiv/lr/runtime/lr_repair_write.hpp"
#include "cursiv/lr/runtime/lr_search_bound.hpp"
#include "cursiv/lr/runtime/lr_states.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/runtime/LexReading.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/runtime/RepairFault.hpp"
#include "cursiv/shared/runtime/TypoWalk.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// The largest count where a product or a sum would wrap: a bound derived from
// a grammar or a document is never smaller for being large.
constexpr std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) {
  return a > ~std::uint64_t{0} - b ? ~std::uint64_t{0} : a + b;
}
constexpr std::uint64_t saturating_mul(std::uint64_t a, std::uint64_t b) {
  return a != 0 && b > ~std::uint64_t{0} / a ? ~std::uint64_t{0} : a * b;
}

// What a link is to the expected set: an arm the drive took is no edit; a
// edit inserts its terminal, or nothing.
inline std::uint32_t lr_search_kind(const LrEdit& g) {
  if (g.kind == LrEdit::arm) return TiedInserts::kArm;
  return g.kind == LrEdit::ins ? std::uint32_t{g.term}
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

// Once an edit's readings are admitted, the tail no queue entry names is
// given back; where none is left and none passed its link on, so are the
// links and cactus nodes they added, which only they named.
inline void lr_search_trim(LrSearch& s, const LrSearchCheckpoint& m) {
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
  if (!fresh && repair_merge<&LrConfig::freeInserts>(
                    s, s.configs, i, it->second, lr_search_kind))
    return;
  lr_search_queue(s, r.cost + h, r, i);
}

// The readings in `s.out` admitted, and what the edit added trimmed.
CURSIV_ALWAYS_INLINE inline void lr_search_admit_out(
    LrSearch& s, const LrSearchCheckpoint& m) {
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
      r.link = s.link(LrEdit{.parent = r.link, .held = r.pin});
      r.armNext = 1;
    }
    LrConfig copy = r;
    copy.queued = false;
    copy.link = s.link(LrEdit{.parent = s.links[copy.link].parent,
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
  // An action refused leaves the reading as it was.
  template <class Act>
  bool attempt(std::size_t i, Act act) {
    ++s.work;
    LrConfig& r = s.configs[i];
    const LrCactusView run = r.run;
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

// What the search walked between its units of work since it last counted, at a
// lexed byte's rate: the bytes the conditions read of long gaps, and the
// siblings the cactus stepped past.
inline void lr_search_count_work(const LrContext& c, LrSearch& s) {
  s.work += s.cactus.stepped_units(kRepairLexBytes);
  const std::uint64_t read = c.gaps.read() - s.gapsCounted;
  if (read < kRepairLexBytes) return;
  s.work += read / kRepairLexBytes;
  s.gapsCounted += read - read % kRepairLexBytes;
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
  lr_search_count_work(c, s);
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
                                     LrEdit::Kind kind, SymbolId term,
                                     std::uint32_t end, std::uint32_t choice) {
  s.configs.push_back(s.configs[from]);
  LrConfig& e = s.configs.back();
  e.link = s.link(LrEdit{.parent = e.link,
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
  e.edits += 1;
  e.fresh = false;
  e.shifts = 0;
  e.dead = e.gone = e.weighed = e.queued = e.pinDue = false;
  ++s.work;
  return static_cast<std::uint32_t>(s.configs.size()) - 1;
}

template <const auto& A>
inline constexpr auto kLrAnyClose = [] {
  std::remove_cvref_t<decltype(A.cold.follow[0])> all{};
  for (const auto& row : A.cold.closePool) all |= row;
  return all;
}();

// A terminal the state shifts outright is needed by the state's own row alone.
template <const auto& A>
inline bool lr_search_maybe_required(std::uint32_t state, SymbolId t) {
  if (!kLrAnyClose<A>.test(t)) return false;
  if (A.cold.close_row(static_cast<std::uint16_t>(state)).test(t)) return true;
  std::uint32_t first = 0;
  unsigned count = 0;
  lr_action_run<A>(state, t, first, count);
  return count != 1 || !lr_takes_token_in<A>(lr_shadow_action<A>(first).kind);
}

// In a section the token that closes it is needed only where the document ends
// inside it.
template <const auto& A>
inline bool lr_search_insertable(std::uint32_t state, SymbolId t, bool atEnd) {
  return t != kEofSymbol && A.cfg.terms[t].synthetic != kSyntheticImplied &&
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
// its reading as its steps would make them, at the work they would count.
template <const auto& A>
void lr_search_forward(LrSearch& s, LrConfig& r, const LrSearch::Chain& ch) {
  const auto stand = [&] {
    const std::uint16_t mode = A.mode[r.run.back()];
    if (r.laMode != mode &&
        (r.laMode == kNoMode || !A.mode_sub(r.laMode, mode) ||
         lr_has_candidates<A>(mode, r.la.sym))) {
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
  r.template shifted<A>(A.cfg.terms[r.la.sym].synthetic != 0);
}

// Its readings go to `s.out`; a required insertion that goes on a run of them
// costs nothing. A token the table refuses outright costs what reading it
// would have, and adds no link: `kNoEditLink`.
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
      return kNoEditLink;
    }
    chained = told == LrChain::shifts && s.work + s.chain.n + 1 <= s.limit;
  }
  const std::uint32_t e = lr_search_child(s, bi, LrEdit::ins, term, 0, choice);
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
    x.cost += cont ? 0 : kRepairEdit;
    x.edits -= cont ? 1u : 0u;
    if (!x.fresh) {
      x.freeInserts =
          x.pinDue ? (cont ? free - 1 : (most > 0 ? most - 1 : 0)) : 0;
    }
    due = due || x.pinDue;
  }
  return link;
}

// The terminals a state offers a repair to insert, in the order the grammar
// wrote them (those its list names first, then the rest by id), each with
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
        lr_search_maybe_required<A>(state, term))
      return true;
    bool due = false;
    const LrSearchCheckpoint pools(s);
    lr_search_insert<A>(c, s, from, term, rank + k, due);
    lr_search_admit_out(s, pools);
    return true;
  });
}

// The token in hand as the lexer's refused reading of it, ending where it
// closes, at each line end that cuts no word, or at the text's end; trivia so
// read is skipped. The ends are found once per search.
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
    const LrSearchCheckpoint pools(s);
    const std::uint32_t e = lr_search_child(
        s, bi, trivia ? LrEdit::skip : LrEdit::rep, r.term, end, choice++);
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
    if (k == ends.found.size()) {
      const TextOffset from = ends.regime == RepairEnds::none ? p : ends.scanned;
      bool cut = false;
      if (!ends.more(c.src, c.len, p, r, s.work, cuts,
                     lr_search_walk_end(c, s, std::min(from, c.len)), cut,
                     c.stop, s.limit)) {
        if (cut) lr_search_cut(s);
        return;
      }
    }
    if (one(ends.found[k]) && ends.regime == RepairEnds::lines) return;
  }
}

// Whether refused reading `b` may go on by a re-reading of the token in hand;
// a walk its share cut may have found one.
template <const auto& A>
bool lr_search_rereads(LrContext& c, LrSearch& s, const LrConfig& b) {
  const bool synthetic =
      b.la.sym < A.cfg.nterms && A.cfg.terms[b.la.sym].synthetic != 0;
  if (b.la.begin >= c.len || synthetic) return false;
  const std::uint32_t state = b.run.back();
  if (lr_search_reading<A>(c, s, A.mode[state], b.la.begin).term !=
      kInvalidSymbol)
    return true;
  return (b.mayReplace() &&
          !lr_search_typos<A>(c, s, state, b.la.begin).empty()) ||
         lr_search_spent(s);
}

// The keyword a state acts on alone, where it acts on no other terminal: a
// word in its place stands for it, whatever the word resembles. None where
// the state admits more, since the word may be the next of them.
template <const auto& A>
SymbolId lr_search_forced(std::uint32_t state) {
  auto acts = A.cold.follow[state];
  const unsigned t = acts.take();
  if (t >= A.cfg.nterms || acts.any() || !A.cfg.terms[t].spelled ||
      !lex_boundary(A.cfg).word_spelled(
          A.cfg.term_text(static_cast<std::uint16_t>(t))) ||
      !lr_search_insertable<A>(state, static_cast<SymbolId>(t), false))
    return kInvalidSymbol;
  return static_cast<SymbolId>(t);
}

// The word in hand read as the keyword its state is forced to, over its
// deletion's span: one edit costed as a deletion and an insertion, or lower
// where the misspelling walk reads it there.
template <const auto& A>
void lr_search_replace(LrContext& c, LrSearch& s, std::uint32_t gi) {
  const std::uint32_t from = s.configs[gi].source();
  const std::uint32_t state = s.configs[from].run.back();
  const std::uint16_t mode = A.mode[state];
  const SymbolId term = lr_search_forced<A>(state);
  const Token la = s.configs[from].la;
  const Token written = lr_search_written<A>(c, s, la, mode);
  const TextOffset end = lr_search_error_end<A>(c, s, mode, la);
  for (const Typo& t : lr_search_typos<A>(c, s, state, la.begin))
    if (t.end == end && t.term == term) return;
  // One spelling held against the table: no walk, so nothing to count.
  const TypoTape<A> tape{
      {.src = c.src, .extent = c.len, .docLen = c.len, .read = nullptr},
      la.begin};
  if (lr_search_spent(s) || !tape.replaces(A.modeStart[mode], term, end))
    return;
  const LrSearchCheckpoint pools(s);
  const std::uint32_t e = lr_search_child(s, from, LrEdit::rep, term, end,
                                          s.configs[gi].insertRank());
  LrConfig& r = s.configs[e];
  lr_pin(r, term, true, end, written, mode);
  r.cost += kRepairReplace;
  r.freeInserts = 0;
  lr_search_read<A>(c, s, e);
  lr_search_admit_out(s, pools);
}

// The edits the canonical form admits, in rank order: re-readings, required
// insertions, the others, the drop, and the abandons one depth at a time.
template <const auto& A>
void lr_search_expand(LrContext& c, LrSearch& s, std::uint32_t bi) {
  const LrConfig b = s.configs[bi];
  const std::uint32_t state = b.run.back();
  const std::uint16_t mode = A.mode[state];
  const bool atEnd = b.la.begin >= c.len;
  const bool synthetic =
      b.la.sym < A.cfg.nterms && A.cfg.terms[b.la.sym].synthetic != 0;
  std::uint32_t choice = 0;
  // The generator counts the fault it opens here, and the least its edits
  // cost: queued, it is not counted again.
  const auto defer = [&](LrConfig g, std::uint32_t extra) {
    g.cost += extra;
    g.edits += 1;
    g.faults += b.fresh ? 1u : 0u;
    s.configs.push_back(g);
    lr_search_queue(s, g.cost, g,
                    static_cast<std::uint32_t>(s.configs.size()) - 1);
  };
  const Token written = lr_search_written<A>(c, s, b.la, mode);
  if (!atEnd && !synthetic) lr_search_lexical<A>(c, s, bi, written, choice);
  if (b.mayReplace() && !atEnd && !synthetic) {
    for (const Typo& t : lr_search_typos<A>(c, s, state, b.la.begin)) {
      if (lr_search_spent(s)) break;
      if (t.term == lr_text_close<A>(state)) continue;
      ++s.work;
      const LrSearchCheckpoint pools(s);
      const std::uint32_t e =
          lr_search_child(s, bi, LrEdit::rep, t.term, t.end, choice++);
      LrConfig& r = s.configs[e];
      lr_pin(r, t.term, true, t.end, written, mode);
      r.cost += t.cost;
      r.freeInserts = 0;
      lr_search_read<A>(c, s, e);
      lr_search_admit_out(s, pools);
    }
  }
  // A word in place of the keyword its state is forced to, unlike it, waits
  // behind one entry, at the cost of its deletion and the keyword's
  // insertion.
  if (b.mayReplace() && !atEnd && !synthetic && written.sym != kInvalidSymbol &&
      written.sym != lr_text_close<A>(state) &&
      lex_boundary(A.cfg).word_at(c.src, b.la.begin, c.len) &&
      lr_search_forced<A>(state) != kInvalidSymbol)
    defer(LrConfig::replaceGenerator(b, bi, choice), kRepairReplace);
  ++choice;
  // Bytes no mode reads refuse every reading the same: insertions in front of
  // them are told apart only past them, so one is made there, never a run.
  const bool garbage =
      !atEnd && !synthetic && written.sym == kInvalidSymbol &&
      lr_search_reading<A>(c, s, mode, b.la.begin).term == kInvalidSymbol;
  if (b.mayInsert(garbage)) {
    // Only a required run costs nothing past its first, so the terminals that
    // cannot be required wait behind one entry until a reading that costly is
    // asked for.
    const std::uint32_t insertRank = choice;
    bool deferred = false;
    lr_each_insert<A>(state, [&](unsigned t, unsigned k) {
      const auto term = static_cast<SymbolId>(t);
      if (!lr_search_insertable<A>(state, term, atEnd)) return true;
      if (lr_search_spent(s)) return false;
      if (!lr_search_maybe_required<A>(state, term)) {
        deferred = true;
        return true;
      }
      bool due = false;
      const LrSearchCheckpoint pools(s);
      const std::uint32_t link =
          lr_search_insert<A>(c, s, bi, term, insertRank + k, due);
      if (!due && link != kNoEditLink) s.links[link].choice += A.cfg.nterms;
      lr_search_admit_out(s, pools);
      return true;
    });
    // Ranks need order, not consecutive values: two terminal-sized ranges
    // put required insertions first, then all others in declaration order.
    choice += 2u * A.cfg.nterms;
    if (deferred) {
      LrConfig g = LrConfig::insertGenerator(b, bi, insertRank + A.cfg.nterms);
      g.invented += 1;
      defer(g, kRepairEdit);
    }
  }
  if (!atEnd && written.sym != lr_text_close<A>(state)) {
    const TextOffset end = lr_search_error_end<A>(c, s, mode, b.la);
    const LrSearchCheckpoint pools(s);
    const std::uint32_t e =
        lr_search_child(s, bi, LrEdit::del, 0, end, choice++);
    LrConfig& r = s.configs[e];
    lr_drop_levels<A>(c, r.virt, r.indent, b.la);
    if (end > r.at) r.at = end;
    if constexpr (kLrConditioned<A>)
      if (end != b.la.begin) r.virt.from = end;
    r.laMode = kNoMode;
    r.cost += kRepairEdit;
    r.lost += synthetic ? 0u : 1u;
    r.phase = LrRepairPhase::discardOnly;
    r.freeInserts = 0;
    lr_search_read<A>(c, s, e);
    lr_search_admit_out(s, pools);
  }
  // Bytes no terminal reads no state takes: giving up frames makes nothing.
  if (b.mayAbandon() && b.run.size() > 1 && b.la.sym < A.cfg.nterms) {
    LrConfig g = LrConfig::abandonGenerator(
        b, bi, b.run.size() - 1, b.run.top(), b.run.begin_node(), choice);
    g.lost += 1;
    defer(g, kRepairEdit);
  }
}

// The shallowest abandon left that gives up text and whose state takes the
// token in hand: made at the cost it was queued at, else queued at its own.
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
    if (!A.cold.follow[s.cactus.state(below)].test(la)) continue;
    const std::int64_t at = s.cactus.begin_of(begin);
    // Counted only as far as this cost reaches: past it, a floor.
    const std::int64_t lost =
        at >= 0
            ? tokens - at
            : tokens +
                  lr_search_src_tokens(
                      c, s, static_cast<std::uint32_t>(-1 - at),
                      std::max<std::int64_t>(
                          (std::int64_t{f} - baseCost) / kRepairEdit - tokens,
                          0));
    if (lost < 1) continue;
    const auto cost = static_cast<std::uint32_t>(baseCost + lost * kRepairEdit);
    const auto lostAll = static_cast<std::uint32_t>(baseLost + lost);
    if (cost > f) {
      requeue(keep, node, begin, cost, lostAll);
      return;
    }
    const LrSearchCheckpoint pools(s);
    const std::uint32_t e =
        lr_search_child(s, g.source(), LrEdit::abandon, 0, keep, g.armNext++);
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

// The goals read on with no edit: `reach[k]` is where candidate `k`'s
// furthest reading was refused, past the blanks the indentation reads there, or
// `kRepairReachAll`. The reading furthest back steps, until all agree or
// `kRepairRaceSteps` pass. True if the share ran out.
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
  const auto reach_all = [&](const std::vector<std::uint32_t>& ks) {
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
  LrSearch::Futures waiting(0, s.seen.hash_function(), s.seen.key_eq());
  // Only at a token boundary can a reading join the one waiting on its future.
  const auto place = [&](std::uint32_t w, std::int64_t at) {
    const LrConfig& q = s.configs[runners[w].cfg];
    if (q.gone || q.dead) {
      TextOffset refused = q.la.begin;
      if constexpr (kIndented<A.cfg>)
        refused = indent_run(c.src, refused, c.len).stop;
      for (const std::uint32_t k : runners[w].of)
        reach[k] = std::max(reach[k], refused);
      return;
    }
    if (q.done) {
      reach_all(runners[w].of);
      return;
    }
    if (q.run.tokens() != at) {
      const auto [it, fresh] = waiting.try_emplace(LrSearch::Future::of(q), w);
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
  std::uint32_t steps = 0;
  while (!queue.empty()) {
    c.stop.poll();
    const bool decided = steps > kRepairRaceSteps || level();
    if (decided || s.work > s.limit) {
      for (const std::uint32_t q : queue) reach_all(runners[q].of);
      return !decided;
    }
    std::ranges::pop_heap(queue, behind);
    const std::uint32_t w = queue.back();
    queue.pop_back();
    const std::uint32_t i = runners[w].cfg;
    if (const auto it = waiting.find(LrSearch::Future::of(s.configs[i]));
        it != waiting.end() && it->second == w)
      waiting.erase(it);
    const std::int64_t at = s.configs[i].run.tokens();
    s.reading.clear();
    lr_search_step<A>(c, x, i);
    lr_search_count_work(c, s);
    if (s.configs[i].run.tokens() != at) ++steps;
    for (const std::uint32_t j : s.reading) {
      runners.push_back(Runner{j, runners[w].of});
      place(static_cast<std::uint32_t>(runners.size()) - 1, at);
    }
    place(w, at);
  }
  return false;
}

// An arm the drive takes on its own is no edit; one taken while an invented
// token is in hand is the writer's, and so the path's.
inline bool lr_same_edits(const std::vector<LrEdit>& links, std::uint32_t a,
                          std::uint32_t b) {
  const auto drives = [&](std::uint32_t l) {
    return l != kNoEditLink && links[l].kind == LrEdit::arm && !links[l].held &&
           links[l].parent != kNoEditLink;
  };
  for (;;) {
    while (drives(a)) a = links[a].parent;
    while (drives(b)) b = links[b].parent;
    if (a == b) return true;
    if (a == kNoEditLink || b == kNoEditLink) return false;
    const LrEdit& x = links[a];
    const LrEdit& y = links[b];
    if (x.parent == kNoEditLink || y.parent == kNoEditLink ||
        x.kind != y.kind || x.term != y.term || x.end != y.end ||
        x.at != y.at || !(x.la == y.la) ||
        (x.kind == LrEdit::arm && x.choice != y.choice))
      return false;
    a = x.parent;
    b = y.parent;
  }
}

// Two paths that make the same edits fold the same.
inline std::uint64_t lr_edits_fold(const std::vector<LrEdit>& links,
                                   std::uint32_t l) {
  std::uint64_t h = 0;
  for (; links[l].parent != kNoEditLink; l = links[l].parent) {
    const LrEdit& g = links[l];
    if (g.kind == LrEdit::arm && !g.held) continue;
    const std::uint64_t v =
        (std::uint64_t{g.kind} << 56) ^ (std::uint64_t{g.term} << 40) ^
        (std::uint64_t{g.la.sym} << 24) ^ (std::uint64_t{g.end} << 20) ^ g.at ^
        (g.kind == LrEdit::arm ? std::uint64_t{g.choice} << 48 : 0);
    h = (h ^ v) * 0x100000001b3ull;
  }
  return (h ^ l) * 0x100000001b3ull;
}

// A plan names nodes of the cactus it was elected on: a search that makes
// another drops it.
inline void lr_search_reset(LrSearch& s) {
  const auto clear = [](auto&... x) { (x.clear(), ...); };
  s.restart();
  s.cactus.reset();
  clear(s.sources, s.configs, s.seen, s.srcTokens, s.lexed, s.readings, s.ends,
        s.typos, s.written, s.ahead, s.misread, s.plan, s.planArms);
  s.errorEnds.reset();
  s.costAhead.reset();
  s.unread.reset();
  s.next = 0;
  s.horizon = 0;
}

// A root is refused, so it costs at least one edit. Adding its frames costs
// as much as reading that many bytes.
template <class Reading>
void lr_search_root(LrSearch& s, const Reading& v, TextOffset at,
                    const LrStack& frames, NodeCount len, std::uint32_t choice,
                    bool fresh) {
  const auto src = static_cast<std::uint32_t>(s.sources.size());
  const LrCactus::Top top = s.cactus.add(frames, src);
  s.work += frames.size() / kRepairLexBytes;
  LrConfig root;
  root.run = LrCactusView(s.cactus, top);
  root.at = at;
  root.la = v.la;
  root.laMode = v.laMode;
  root.virt = v.virt;
  root.indent = v.indent;
  root.link = s.link(LrEdit{.choice = choice});
  root.fresh = fresh;
  s.sources.push_back(LrSearch::Source{.frames = &frames, .scanned = len});
  s.horizon = std::max(s.horizon, v.la.begin + kLrBoundReach);
  s.configs.push_back(root);
  lr_search_queue(s, kRepairReread, root,
                  static_cast<std::uint32_t>(s.configs.size()) - 1);
}

inline void lr_search_start(const LrContext& c, LrSearch& s) {
  lr_search_reset(s);
  s.costBase = c.cost;
  // A fault a stopped search left open goes on.
  lr_search_root(s, c, c.committed, c.stack, c.tape.len(), 0, !s.fault.open);
}

// With no goal before the work ran out, the reading that got furthest.
template <const auto& A>
std::uint32_t lr_search_run(LrContext& c, LrSearch& s, bool& exhausted,
                            bool& raced) {
  s.gapsCounted = c.gaps.read();
  std::uint32_t level = 0;
  std::uint64_t levelWork = s.work;
  bool bounds = false;
  constexpr std::uint32_t kNone = kLrNoConfig;
  std::uint32_t furthest = kNone;
  std::uint32_t finished = kNone;
  exhausted = false;
  raced = false;
  while (!s.heap.empty()) {
    c.stop.poll();
    const RepairEntry e = s.pop();
    if (s.configs[e.at].gone) continue;
    if (!s.goals.empty() && e.f > s.configs[s.goals[0]].cost) break;
    // A finished reading already reaches the whole text. A queued reading
    // with a worse least key cannot beat it, even after the race. Equal keys
    // stay in the search so their ranks and alternative insertions survive.
    if (finished != kNone) {
      const auto& g = s.configs[finished];
      if (std::array{e.f, e.lost, e.edits, e.faults, e.invented} >
          std::array{g.cost, g.lost, g.edits, g.faults, g.invented})
        break;
    }
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
      case LrWorkKind::forcedReplacement:
        lr_search_replace<A>(c, s, e.at);
        continue;
      case LrWorkKind::enumerateAbandons:
        lr_search_abandon<A>(c, s, e.at, e.f);
        continue;
    }
    const LrConfig& x = s.configs[e.at];
    if (lr_search_goal(x)) {
      if (x.done && (finished == kNone ||
                     repair_key_cmp(x, s.configs[finished]) < 0))
        finished = e.at;
      s.goals.push_back(e.at);
      continue;
    }
    if (furthest == kNone || x.at > s.configs[furthest].at) furthest = e.at;
    // Queued as if a re-reading went on from it: where none does, it costs a
    // whole edit.
    if (x.freeInserts == 0 && e.f < x.cost + kRepairEdit &&
        !lr_search_rereads<A>(c, s, x)) {
      lr_search_queue(s, x.cost + kRepairEdit, x, e.at, e.lost - x.lost);
      continue;
    }
    // Past `boundAfter` of work at one cost, each reading is bounded once and
    // waits if it costs more than it was queued at.
    if (e.f != level) {
      level = e.f;
      levelWork = s.work;
    }
    bounds = bounds || s.work - levelWork >= s.boundAfter;
    if (bounds && !x.weighed) {
      s.configs[e.at].weighed = true;
      const std::uint32_t bound = lr_search_bound<A>(c, s, x);
      const std::uint32_t f = x.cost + bound / kLrCostScale;
      const std::uint32_t lost = bound % kLrCostScale;
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
  return repair_elect<lr_edits_fold, lr_same_edits, lr_search_kind>(
      s, s.configs, true, c.stop, [&](const auto& of, auto& reach) {
        raced = lr_search_race<A>(c, s, of, reach);
      });
}

// `open`: a path elected short of a goal stops inside its last fault, for the
// next search to go on with.
inline void lr_search_plan(const LrContext& c, LrSearch& s, std::uint32_t ci,
                           bool open) {
  std::vector<std::uint32_t> path;
  for (std::uint32_t l = s.configs[ci].link;
       l != kNoEditLink && s.links[l].parent != kNoEditLink;
       l = s.links[l].parent) {
    path.push_back(l);
  }
  s.plan.clear();
  s.planArms.clear();
  s.next = 0;
  for (std::size_t k = path.size(); k-- > 0;) {
    const LrEdit& g = s.links[path[k]];
    if (g.kind != LrEdit::arm) {
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
    st.before = s.costBase + st.g.cost;
    st.stepCost =
        (k + 1 < s.plan.size() ? s.plan[k + 1].g.cost : s.configs[ci].cost) -
        st.g.cost;
    st.kept = c.stack[std::max(st.g.low, 1u) - 1];
  }
  if (open && !s.plan.empty() && !lr_search_goal(s.configs[ci]))
    s.plan.back().closes = false;
  if (!s.plan.empty() && s.plan[0].g.kind == LrEdit::ins)
    std::erase(s.also, static_cast<ElementId>(s.plan[0].g.term));
  else
    s.also.clear();
}

// The pause a step was made at: the token in hand, what it cost, and the
// stack above the frames its reading never moved, read only as deep as it
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
         "a reading never adds a note behind the last one");
  if (s.bounded && n.begin < s.boundedBegin) s.boundedBegin = n.begin;
  c.notes.push_back(std::move(n));
  f = RepairFault{};
}

// What a construct abandoned in `state` still needed: nothing named where its
// row is empty.
template <const auto& A>
ElementId lr_search_missing(std::uint32_t state) {
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
    case LrEdit::ins:
      if (!lr_write_token<A>(c, st.g.term, false, 0, Token{},
                             s.planArms.data() + st.armBase, st.armCount))
        return false;
      if (!f.ins) {
        f.ins = true;
        f.first = static_cast<ElementId>(st.g.term);
        if (first) f.also = s.also;
      }
      break;
    case LrEdit::rep: {
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
    case LrEdit::skip:
      if (!f.del && !f.abandon) f.begin = c.la.begin;
      f.rep = true;
      f.read = static_cast<ElementId>(st.g.term);
      lr_write_skip<A>(c, st.g.term, st.g.end);
      f.end = st.g.end;
      break;
    case LrEdit::del: {
      const TextOffset end = lr_error_end<A>(c, A.mode[state], c.la);
      if (!f.del && !f.abandon) {
        f.begin = c.la.begin;
        if constexpr (kIndented<A.cfg>)
          f.mixedIndent =
              indent_refused(c.src, c.len, c.indentArena, c.indent, c.committed,
                             c.la, [&c] { return c.tape.held(); });
      }
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
    case LrEdit::abandon: {
      f.missing = lr_search_missing<A>(state);
      f.abandon = true;
      f.begin = st.g.end < c.stack.size() ? c.stack[st.g.end].begin
                                          : c.committed;
      lr_write_abandon<A>(c, st.g.end, kNoRow);
      f.end = c.committed;
      break;
    }
    case LrEdit::arm:
      break;
  }
  c.cost += st.stepCost;
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

// At the end of the text, the insertions `configs[0]` still needs, in a row:
// each the first its row offers that its shift needs, until it accepts, needs
// nothing or reaches the bound. Whether it made any.
template <const auto& A>
bool lr_search_closers(LrContext& c, LrSearch& s) {
  lr_search_start(c, s);
  s.limit = s.degradedLimit;
  s.gapsCounted = c.gaps.read();
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
          !lr_search_maybe_required<A>(s.configs[0].run.back(), term))
        return true;
      const LrSearchCheckpoint pools(s);
      const std::uint32_t e = lr_search_child(s, 0, LrEdit::ins, term, 0, 0);
      LrConfig& r = s.configs[e];
      lr_pin(r, term, false, 0, Token{}, A.mode[r.run.back()]);
      r.cost += kRepairEdit;
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

// The first edit that holds: the token dropped, or at the end the required
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
    st.stepCost = kRepairEdit;
    st.g.kind = LrEdit::del;
    lr_search_write<A>(c, s, st, false);
    s.degradedShifts = c.shifts;
    s.boundedEnd = c.committed;
    return lr_resume(c);
  }
  if (s.degradedAt != c.committed) {
    s.degradedAt = c.committed;
    s.degradedRun = 0;
    // What one search may hold: the completion a grammar needs can be as long
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
  f.missing = lr_search_missing<A>(state);
  f.begin = c.stack.size() > 1 ? c.stack[1].begin : c.committed;
  lr_write_abandon<A>(c, 1, A.cfg.entryRow);
  c.cost += kRepairEdit;
  f.end = c.committed;
  lr_search_note(c, s);
  s.boundedEnd = c.committed;
  lr_flush_trivia<A>(c);
  return LrExit::accept;
}

// A pause the reading goes on from without a search, `e` where it stops next: a
// dedent the end hands over for a level a repair dropped is a blank. The plan's
// next step is taken where it was made; elsewhere the plan is dropped.
template <const auto& A>
bool lr_search_goes_on(LrContext& c, LrSearch& s, LrExit& e) {
  if constexpr (kSyntheticTerm<A.cfg, kSyntheticDedent> != 0) {
    if (c.la.sym == kSyntheticTerm<A.cfg, kSyntheticDedent> &&
        c.indent.head != kNoIndent && c.indentArena[c.indent.head].mute) {
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
// refuses it, the first edit that holds.
template <const auto& A>
LrExit lr_search_take(LrContext& c, LrSearch& s, std::uint32_t elected,
                      bool exhausted, bool raced, bool open) {
  const TextOffset from = c.la.begin;
  if (exhausted)
    ++c.stats.budgetStops;
  else if (raced)
    ++c.stats.reachStops;
  if (exhausted || raced) lr_search_open(s, from, s.costBase + kRepairReread);
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
  lr_search_open(s, from, s.costBase + kRepairReread);
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

}  // namespace cursiv::engine
