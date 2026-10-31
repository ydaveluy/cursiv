// The least a refused reading still pays before three tokens hold it, read
// from the text ahead under every mode and the terminals the table lets
// follow one another. No path the search takes is cheaper: the order it
// elects in stands.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrConfig.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrSearch.hpp"
#include "cursiv/engines/lr/runtime/lr_lex_reading.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/lr/runtime/lr_repair_write.hpp"
#include "cursiv/engines/lr/runtime/lr_rescan.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// What a reading still owes, as the search orders it: cost paid above, the
// author's tokens lost below.
inline constexpr std::uint32_t kLrPaid = 1u << 16;
inline constexpr std::uint32_t kLrLost = 1u;
inline constexpr std::uint32_t kLrOwedReread = kRepairReread * kLrPaid;
inline constexpr std::uint32_t kLrOwedGesture = kRepairGesture * kLrPaid;
// How far past its start a search's bound reads: a bound on work, not a
// property of the grammar.
inline constexpr TextOffset kLrBoundReach = 512;
// A cost no reading reaches: nothing the search can do ends in a hold. Every
// token the bound reads costs at most one gesture, so it stays above any sum
// the bound makes within its reach.
inline constexpr std::uint32_t kLrNoBound =
    2 * kLrBoundReach * kLrOwedGesture;

// Levels, implied tokens and joins are the reading's own, not the text's: a
// grammar that lays any token keeps the plain bound.
template <const auto& A>
inline constexpr bool kLrBounded = [] {
  if (kIndented<A.cfg>) return false;
  for (unsigned t = 0; t < A.cfg.nterms; ++t)
    if (A.cfg.terms[t].laid != 0) return false;
  return true;
}();

// Each rival `mode` reads where `t` stands.
template <const auto& A, class Each>
void lr_search_rivals(const LrContext& c, std::uint16_t mode, const Token& t,
                      Each each) {
  if constexpr (A.maxCands > 1) {
    if (A.modeMulti[mode] == 0 || c.drive->scanText == nullptr ||
        t.sym >= A.cfg.nterms)
      return;
    ScanRoom<A.maxCands> room;
    const std::uint32_t n =
        c.drive->scanText(c.src, c.len, mode, t.begin, room.data(), A.maxCands);
    for (std::uint32_t q = 0; q < n; ++q)
      if (room[q].term != kInvalidSymbol && !lr_is_trivia<A>(room[q].term))
        each(Token{t.begin, room[q].end, room[q].term});
  } else {
    (void)c, (void)mode, (void)t, (void)each;
  }
}

// Where the work in flight must stop: the search's share, and while the bound
// weighs, a walk that could not read its token whole.
inline bool lr_search_spent(const LrSearch& s) {
  return (s.bounding && s.boundShort) || s.work > s.limit;
}

// A walk its end cut stops what it was paid from: the search, which says a
// bound decided it, or the bound, which then knows nothing.
inline void lr_search_cut(LrSearch& s) {
  if (s.bounding)
    s.boundShort = true;
  else if (s.work <= s.limit)
    s.work = s.limit + 1;
}

// The bytes the walk in flight may still read: what the share pays for, and
// while the bound weighs, no further than its reach -- so the bound never
// spends the share.
inline std::uint64_t lr_search_bytes(const LrSearch& s) {
  const std::uint64_t bytes = repair_bytes(s.work, s.limit);
  return s.bounding ? std::min<std::uint64_t>(bytes, kLrBoundReach) : bytes;
}

// Where a lexical walk from `p` reads the text as ending: as far as what it is
// paid from still pays for.
inline TextOffset lr_search_walk_end(const LrContext& c, const LrSearch& s,
                                     TextOffset p) {
  const std::uint64_t bytes = lr_search_bytes(s);
  return c.len - p > bytes ? static_cast<TextOffset>(p + bytes) : c.len;
}

// A walk from `p` read to `reached`, paid for; true where the end it was
// given cut it, and nothing it read is known.
inline bool lr_search_walked(const LrContext& c, LrSearch& s, TextOffset p,
                             TextOffset end, TextOffset reached) {
  s.work += (reached - p) / kRepairLexBytes;
  if (end >= c.len || reached < end) return false;
  lr_search_cut(s);
  return true;
}

// What a deletion of `t` takes under `mode`, the bytes walked paid for. A
// walk a unit pays for is read again, cheaper than kept; a longer one is read
// once per search.
template <const auto& A>
TextOffset lr_search_error_end(const LrContext& c, LrSearch& s,
                               std::uint16_t mode, const Token& t) {
  if (t.begin >= c.len || t.sym != kInvalidSymbol)
    return lr_error_end<A>(c, mode, t);
  if (c.len - t.begin > kRepairLexBytes) {
    const TextOffset near = t.begin + kRepairLexBytes;
    TextOffset read = t.begin;
    const TextOffset e = lr_error_end<A>(c, mode, t, near, read);
    if (read < near) return e;
  }
  const std::uint64_t key = (std::uint64_t{t.begin} << 16) | mode;
  if (const TextOffset* hit = s.errorEnds.find(key)) return *hit;
  const TextOffset end = lr_search_walk_end(c, s, t.begin);
  TextOffset reached = t.begin;
  const TextOffset e = lr_error_end<A>(c, mode, t, end, reached);
  if (!lr_search_walked(c, s, t.begin, end, reached))
    s.errorEnds.emplace(key).first = e;
  return e;
}

// The lexer's refused reading at `p` under `mode`, read once per search; none
// where the share cut it.
template <const auto& A>
const LexReading& lr_search_reading(const LrContext& c, LrSearch& s,
                                    std::uint16_t mode, TextOffset p) {
  static constexpr LexReading kNone{};
  if (p >= c.len ||
      !A.derived.lexOpeners[mode].has(static_cast<unsigned char>(c.src[p])))
    return kNone;
  const std::uint64_t key = (std::uint64_t{p} << 16) | mode;
  if (const auto it = s.readings.find(key); it != s.readings.end())
    return it->second;
  const TextOffset end = lr_search_walk_end(c, s, p);
  TextOffset reached = p;
  const LexReading r = lr_lex_reading<A>(c.src, end, mode, p, reached);
  ++s.work;
  if (lr_search_walked(c, s, p, end, reached)) return kNone;
  return s.readings.emplace(key, r).first->second;
}

// The token at `from` under `mode`, its walks paid for; `cut` where the end
// they were given cut them. Its tokens are shared by position, whatever
// reading asks: they are read under the context's bracket depth.
template <const auto& A>
Token lr_search_scan(const LrContext& c, LrSearch& s, std::uint16_t mode,
                     TextOffset from, bool& cut, bool* refused = nullptr) {
  const TextOffset end = lr_search_walk_end(c, s, from);
  TextOffset reached = from;
  const Token t = lr_scan_at<A, true>(c, mode, from, c.indent.depth, refused,
                                      end, &reached);
  cut = lr_search_walked(c, s, from, end, reached);
  return t;
}

// The token in hand as the mode that lexes everything reads it where its own
// read nothing: one lexeme, read once per search -- in a section, as the
// state's grammar reads it, which is no reading of the whole search.
template <const auto& A>
Token lr_search_written(const LrContext& c, LrSearch& s, const Token& t,
                        std::uint16_t mode) {
  if (t.sym != kInvalidSymbol || t.begin >= c.len) return t;
  if constexpr (A.errModeOf.size() != 0) {
    ++s.work;
    return lr_written<A>(c, t, mode);
  }
  if (const auto it = s.written.find(t.begin); it != s.written.end())
    return it->second;
  ++s.work;
  return s.written.emplace(t.begin, lr_written<A>(c, t)).first->second;
}

// Every token some mode reads from `p`, its rivals included, and where a
// mode reads none, the bytes a deletion there drops. Where a walk could not
// read a token whole, nothing: the bound knows nothing there.
template <const auto& A>
const LrSearch::Ahead& lr_search_ahead(const LrContext& c, LrSearch& s,
                                       TextOffset p) {
  static const LrSearch::Ahead kUnread{};
  if (s.unread.find(p) != nullptr) {
    s.boundShort = true;
    return kUnread;
  }
  if (const auto it = s.ahead.find(p); it != s.ahead.end()) return it->second;
  ++s.work;
  LrSearch::Ahead out;
  const auto add = [](std::vector<Token>& v, const Token& t) {
    for (const Token& o : v)
      if (o == t) return;
    v.push_back(t);
  };
  for (unsigned m = 0; m < A.modeStart.size() && !lr_search_spent(s); ++m) {
    const auto mode = static_cast<std::uint16_t>(m);
    bool refused = false;
    bool cut = false;
    const Token t = lr_search_scan<A>(c, s, mode, p, cut, &refused);
    if (!cut && !out.refused && refused &&
        lr_search_reading<A>(c, s, mode, t.begin).term != kInvalidSymbol)
      out.refused = true;
    if (!cut && t.sym == kInvalidSymbol) {
      add(out.dropped,
          Token{t.begin, lr_search_error_end<A>(c, s, mode, t), t.sym});
      continue;
    }
    if (s.boundShort) break;
    add(out.tokens, t);
    lr_search_rivals<A>(c, mode, t,
                        [&](const Token& u) { add(out.tokens, u); });
  }
  if (s.boundShort) {
    s.unread.emplace(p);
    return kUnread;
  }
  return s.ahead.emplace(p, std::move(out)).first->second;
}

// How far a re-reading walk may read past its token: what the share it is
// paid from still pays for, over all its paths together.
inline TextOffset lr_search_reach(const LrSearch& s) {
  const std::uint64_t bytes = lr_search_bytes(s);
  return bytes < kLrNoOpen ? static_cast<TextOffset>(bytes) : kLrNoOpen;
}

// The walk's bytes paid for. One the share cut spends what it was paid from.
inline void lr_search_charge(LrSearch& s, std::uint64_t read, bool cut) {
  s.work += 1 + read / kRepairLexBytes;
  if (cut) lr_search_cut(s);
}

// Re-readings a cut walk found are not all there are: they are handed out
// once, never kept.
template <const auto& A>
const std::vector<Typo>& lr_search_typos(LrContext& c, LrSearch& s,
                                         std::uint32_t state, TextOffset at) {
  const std::uint64_t key = (std::uint64_t{state} << 32) | at;
  if (const auto hit = s.typos.find(key); hit != s.typos.end())
    return hit->second;
  std::uint64_t read = 0;
  bool cut = false;
  // The bound's walks are each held to its reach; the search's paths share
  // what its share pays for.
  const TextOffset reach = lr_search_reach(s);
  std::vector<Typo> found = lr_rescan_candidates<A>(
      c, state, at, reach, read, cut,
      s.bounding ? ~std::uint64_t{0} : std::uint64_t{reach}, &s.typoScratch);
  lr_search_charge(s, read, cut);
  if (cut) {
    s.cutTypos = std::move(found);
    return s.cutTypos;
  }
  return s.typos.emplace(key, std::move(found)).first->second;
}

// Every re-reading some state may make of the bytes at `at`.
template <const auto& A>
const std::vector<Typo>& lr_search_misread(const LrContext& c, LrSearch& s,
                                           TextOffset at) {
  static const std::vector<Typo> kUnread{};
  if (s.unread.find(at) != nullptr) {
    s.boundShort = true;
    return kUnread;
  }
  if (const auto it = s.misread.find(at); it != s.misread.end())
    return it->second;
  std::vector<Typo> out;
  std::uint64_t read = 0;
  std::uint32_t stops = 0;
  bool cut = false;
  if (at < c.len) {
    const TextOffset reach = lr_search_reach(s);
    const TextOffset end = lr_search_walk_end(c, s, at);
    TextOffset halt = at;
    const TextOffset plain =
        lr_match_halt<A>(c.src, end, A.errMode, at, halt).end;
    read += halt - at;
    cut = end < c.len && halt >= end;
    const TextOffset horizon = typo_horizon(at, plain, c.len, reach);
    for (unsigned m = 0; m < A.modeStart.size() && !cut; ++m) {
      auto want = A.derived.modeWant[m];
      if (want.none()) continue;
      const TypoBounds bounds{.src = c.src,
                              .extent = horizon,
                              .docLen = c.len,
                              .read = &read,
                              .shortOf = &cut,
                              .scratch = &s.typoScratch};
      typo_each(LrTypoTape<A>{bounds, &want, &stops}, A.modeStart[m], at, plain,
                [&out](const Typo& t) {
                  for (const Typo& o : out)
                    if (o.term == t.term && o.end == t.end) return;
                  out.push_back(t);
                });
    }
  }
  lr_search_charge(s, read, cut);
  if (cut) {
    s.unread.emplace(at);
    return kUnread;
  }
  return s.misread.emplace(at, std::move(out)).first->second;
}

// Every token some mode reads at `p`, to `take`, with, where its mode skipped
// trivia to it, what every mode reads from its own start, taken `again`; then
// every token there, read or not, to `drop`.
template <const auto& A, class Take, class Drop>
CURSIV_ALWAYS_INLINE inline void lr_search_each(const LrContext& c, LrSearch& s,
                                                TextOffset p, Take take,
                                                Drop drop) {
  const LrSearch::Ahead& ahead = lr_search_ahead<A>(c, s, p);
  for (const Token& t : ahead.tokens) {
    take(t, false);
    if (t.begin != p)
      for (const Token& u : lr_search_ahead<A>(c, s, t.begin).tokens)
        take(u, true);
  }
  for (const Token& t : ahead.tokens) drop(t);
  for (const Token& t : ahead.dropped) drop(t);
}

// The least a reading pays from `p` to three tokens held: the last token it
// took is `prev` (`kInvalidSymbol` for any) and `run` real tokens were taken
// since its last gesture. A token follows `prev` for free where the table
// lets it, and otherwise after a gesture that reads no text, never a
// re-reading; a token dropped or read as another costs its gesture. Past the
// search's horizon nothing is known, so nothing is owed. What it pays that
// drops the token it stands on is the same walk's other call.
//
// The two call one another once per token ahead, as far as the horizon
// reaches, so they run on a stack of their own: the text does not set the
// native stack's depth. Each call is a frame that resumes at its phase with
// the value of the call it made; the calls, their order and the work they
// spend are those a recursion would make.
template <const auto& A>
class LrSearchOwes {
 public:
  using Frame = LrSearch::Owe;
  using Phase = LrSearch::OwePhase;

  LrSearchOwes(const LrContext& c, LrSearch& s)
      : _c(c), _s(s), _st(s.oweStack), _base(s.oweStack.size()) {}

  std::uint32_t owes(TextOffset p, SymbolId prev, std::uint32_t run) {
    enter_owes(p, prev, run);
    return run_all();
  }

  std::uint32_t dropped(const Token& t, SymbolId prev) {
    enter_dropped(t, prev);
    return run_all();
  }

 private:
  const LrContext& _c;
  LrSearch& _s;
  std::vector<Frame>& _st;
  const std::size_t _base;
  std::uint32_t _ret = 0;

  std::uint32_t run_all() {
    while (_st.size() > _base) step();
    return _ret;
  }

  // The call's value, where its frame was entered or ended.
  void leave(std::uint32_t value) {
    _ret = value;
    _st.pop_back();
  }

  // The frame for `owes(p, prev, run)`, or the value it has at once.
  void enter_owes(TextOffset p, SymbolId prev, std::uint32_t run) {
    if (p >= _s.horizon || lr_search_spent(_s)) {
      _ret = 0;
      return;
    }
    const std::uint64_t key =
        (std::uint64_t{p} << 20) | (std::uint64_t{prev} << 2) | run;
    if (const std::uint32_t* hit = _s.owes.find(key)) {
      _ret = *hit;
      return;
    }
    Frame f;
    f.phase = Phase::outer;
    f.p = p;
    f.prev = prev;
    f.key = key;
    f.best = kLrNoBound;
    f.run = run;
    f.ahead = &lr_search_ahead<A>(_c, _s, p);
    _st.push_back(f);
  }

  // The frame for `dropped(t, prev)`, or the value it has at once.
  void enter_dropped(const Token& t, SymbolId prev) {
    if (t.begin >= _c.len) {
      _ret = kLrNoBound;
      return;
    }
    // A refused token re-read as the lexer's superset owes a re-reading, and
    // where it ends is the text's to say.
    if (lr_search_ahead<A>(_c, _s, t.begin).refused) {
      _ret = kLrOwedReread;
      return;
    }
    const TextOffset end = t.sym == kInvalidSymbol
                               ? t.end
                               : (t.end > t.begin ? t.end : t.begin + 1);
    Frame f;
    f.phase = Phase::droppedFirst;
    f.t = t;
    f.prev = prev;
    _st.push_back(f);
    enter_owes(end, prev, 0);
  }

  // The frame on top takes `t` as its next token: the call it makes, if any,
  // resumes at `ret`; where it makes none, at `next`.
  void take(const Token& t, bool again, Phase next, Phase ret) {
    Frame& f = _st.back();
    // A token read again after a gesture never follows for free.
    const bool follows =
        f.prev == kInvalidSymbol ||
        (!again && A.derived.after[f.prev].test(t.sym));
    const std::uint32_t cost = follows ? 0 : kLrOwedGesture;
    f.phase = next;
    if (cost >= f.best) return;
    const std::uint32_t held = follows ? f.run + 1 : 1;
    if (t.sym == kEofSymbol || held >= kRepairValidateShifts) {
      f.best = cost;
      return;
    }
    f.cost = cost;
    f.phase = ret;
    enter_owes(t.end, t.sym, held);
  }

  void step() {
    Frame& f = _st.back();
    switch (f.phase) {
      case Phase::outer:
        if (f.i == f.ahead->tokens.size()) {
          f.i = 0;
          f.phase = Phase::drop;
          return;
        }
        take(f.ahead->tokens[f.i], false, Phase::more, Phase::outerRet);
        return;
      case Phase::outerRet:
        f.best = std::min(f.best, f.cost + _ret);
        f.phase = Phase::more;
        return;
      // Where its mode skipped trivia to the token, what every mode reads
      // from its own start, taken again.
      case Phase::more:
        if (f.ahead->tokens[f.i].begin != f.p) {
          f.more = &lr_search_ahead<A>(_c, _s, f.ahead->tokens[f.i].begin);
          f.j = 0;
          f.phase = Phase::inner;
        } else {
          ++f.i;
          f.phase = Phase::outer;
        }
        return;
      case Phase::inner:
        if (f.j == f.more->tokens.size()) {
          ++f.i;
          f.phase = Phase::outer;
          return;
        }
        take(f.more->tokens[f.j++], true, Phase::inner, Phase::innerRet);
        return;
      case Phase::innerRet:
        f.best = std::min(f.best, f.cost + _ret);
        f.phase = Phase::inner;
        return;
      // Every token there, read or not, dropped.
      case Phase::drop: {
        const std::size_t read = f.ahead->tokens.size();
        if (f.i == read + f.ahead->dropped.size()) {
          finish(f);
          return;
        }
        const Token t = f.i < read ? f.ahead->tokens[f.i]
                                   : f.ahead->dropped[f.i - read];
        ++f.i;
        if (f.best <= kLrOwedReread) return;
        f.phase = Phase::dropRet;
        enter_dropped(t, f.prev);
        return;
      }
      case Phase::dropRet:
        f.best = std::min(f.best, _ret);
        f.phase = Phase::drop;
        return;
      case Phase::droppedFirst:
        f.best = kLrOwedGesture + kLrLost + _ret;
        f.typos = &lr_search_misread<A>(_c, _s, f.t.begin);
        f.j = 0;
        f.phase = Phase::droppedTypo;
        return;
      case Phase::droppedTypo: {
        if (f.j == f.typos->size()) {
          leave(f.best);
          return;
        }
        const Typo& y = (*f.typos)[f.j++];
        if (f.best <= kLrOwedReread) return;
        f.phase = Phase::droppedTypoRet;
        enter_owes(y.end, y.term, 0);
        return;
      }
      case Phase::droppedTypoRet:
        f.best = std::min(f.best, kLrOwedReread + _ret);
        f.phase = Phase::droppedTypo;
        return;
    }
  }

  // The frame's least is known: kept, unless a walk could not read whole.
  void finish(const Frame& f) {
    const std::uint32_t best = f.best;
    if (!_s.boundShort) _s.owes.emplace(f.key).first = best;
    leave(best);
  }
};

// The least a reading pays that drops the token `t` it stands on, or reads
// its bytes as another, the last token it took being `prev`.
template <const auto& A>
std::uint32_t lr_search_dropped(const LrContext& c, LrSearch& s, const Token& t,
                                SymbolId prev) {
  return LrSearchOwes<A>(c, s).dropped(t, prev);
}

// The least a reading pays from `p` to three tokens held.
template <const auto& A>
std::uint32_t lr_search_owes(const LrContext& c, LrSearch& s, TextOffset p,
                             SymbolId prev, std::uint32_t run) {
  return LrSearchOwes<A>(c, s).owes(p, prev, run);
}

// From `t` or a rival its mode reads there that `state` acts on, the least
// paid to three tokens held; `kLrNoBound` where it acts on none.
template <const auto& A>
std::uint32_t lr_search_takes(const LrContext& c, LrSearch& s,
                              std::uint32_t state, const Token& t) {
  std::uint32_t least = kLrNoBound;
  const auto one = [&](const Token& u) {
    if (u.sym >= A.cfg.nterms || !A.cold.follow[state].test(u.sym)) return;
    const std::uint32_t v =
        u.sym == kEofSymbol ? 0 : lr_search_owes<A>(c, s, u.end, u.sym, 1);
    least = std::min(least, v);
  };
  one(t);
  lr_search_rivals<A>(c, A.mode[state], t, one);
  return least;
}

// The least a refused reading still pays, `free` in an owed run. Its own
// state reads what is dropped where it stands until a token is taken, or a
// misspelling of it; after an insertion or an abandon the state is anyone's,
// and the text alone answers.
template <const auto& A>
std::uint32_t lr_search_weigh(LrContext& c, LrSearch& s, const LrConfig& b,
                              bool free) {
  const std::uint32_t state = b.run.back();
  const std::uint16_t mode = A.mode[state];
  if (!free && b.la.begin < c.len &&
      lr_search_reading<A>(c, s, mode, b.la.begin).term != kInvalidSymbol)
    return kLrOwedReread;
  std::uint32_t best = kLrNoBound;
  if (b.mayReplace()) {
    // Its first gesture is not weighed: it may leave any token acceptable. It
    // reads no text: the re-readings of the token in hand are weighed apart.
    const std::uint32_t gap = free ? 0 : kLrOwedGesture;
    const auto keep = [&](const Token& t, bool) {
      if (gap >= best) return;
      best =
          std::min(best, t.sym == kEofSymbol
                             ? gap
                             : gap + lr_search_owes<A>(c, s, t.end, t.sym, 1));
    };
    const auto drop = [&](const Token& t) {
      if (best > gap + kLrOwedReread)
        best =
            std::min(best, gap + lr_search_dropped<A>(c, s, t, kInvalidSymbol));
    };
    for (const TextOffset q : {b.at, b.la.begin})
      lr_search_each<A>(c, s, q, keep, drop);
    if (free || best <= kLrOwedReread) return best;
  }
  if (b.la.begin >= c.len) return best;
  // A text no mode reads whole may be read again, after the tokens dropped in
  // front of it as it stands: a trivia left open is skipped.
  if (lr_search_ahead<A>(c, s, b.la.begin).refused)
    best = std::min(best, kLrOwedReread);
  // Dropped in its own state, one token after another, until one is taken.
  Token t = b.la;
  for (std::uint32_t k = 1; k * kLrOwedGesture < best && !lr_search_spent(s);
       ++k) {
    ++s.work;
    bool cut = false;
    t = lr_search_scan<A>(c, s, mode, lr_search_error_end<A>(c, s, mode, t),
                          cut);
    if (cut) break;
    const std::uint32_t dropped = k * (kLrOwedGesture + kLrLost);
    if (t.begin < c.len && lr_search_ahead<A>(c, s, t.begin).refused)
      best = std::min(best, dropped + kLrOwedReread);
    if (t.sym == kEofSymbol) {
      if (A.cold.follow[state].test(kEofSymbol)) best = std::min(best, dropped);
      break;
    }
    best = std::min(best, dropped + lr_search_takes<A>(c, s, state, t));
    if (t.begin >= c.len) break;
    if (t.begin >= s.horizon) {
      best = std::min(best, dropped);
      break;
    }
  }
  // Read as another in its own state.
  const bool laid = b.la.sym < A.cfg.nterms && A.cfg.terms[b.la.sym].laid != 0;
  if (b.mayReplace() && !laid && best > kLrOwedReread) {
    for (const Typo& y : lr_search_typos<A>(c, s, state, b.la.begin))
      if (best > kLrOwedReread)
        best = std::min(
            best, kLrOwedReread + lr_search_owes<A>(c, s, y.end, y.term, 0));
  }
  return best;
}

// The least a refused reading still pays. The bound's walks read no further
// than its reach, never past the share: one that could not read a token whole
// leaves it knowing nothing, and the reading owes one gesture.
template <const auto& A>
std::uint32_t lr_search_bound(LrContext& c, LrSearch& s, const LrConfig& b) {
  const bool free = b.freeInserts != 0;
  const std::uint32_t trivial = free ? 0 : kLrOwedReread;
  if constexpr (!kLrBounded<A>) {
    return trivial;
  } else {
    s.bounding = true;
    s.boundShort = false;
    const std::uint32_t owed = lr_search_weigh<A>(c, s, b, free);
    s.bounding = false;
    return s.boundShort ? trivial : owed;
  }
}

}  // namespace cursiv
