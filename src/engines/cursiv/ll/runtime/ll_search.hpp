// The repair search: best first over the edits a refused reading can make,
// each read ahead until three tokens past its last edit hold and one arm
// stands. The cheapest that reads furthest wins, written one edit per pause.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/ll/runtime/LlAtoms.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/LlEdit.hpp"
#include "cursiv/ll/runtime/LlReading.hpp"
#include "cursiv/ll/runtime/LlRemaining.hpp"
#include "cursiv/ll/runtime/LlSearch.hpp"
#include "cursiv/ll/runtime/ll_indent.hpp"
#include "cursiv/ll/runtime/ll_lex_reading.hpp"
#include "cursiv/ll/runtime/ll_lookahead.hpp"
#include "cursiv/ll/runtime/ll_note.hpp"
#include "cursiv/ll/runtime/ll_read.hpp"
#include "cursiv/ll/runtime/ll_rescan.hpp"
#include "cursiv/ll/runtime/ll_section.hpp"
#include "cursiv/ll/table/LlSimulation.hpp"
#include "cursiv/ll/table/LlTable.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/runtime/TypoCandidates.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/shared/table/WriteKind.hpp"

namespace cursiv::engine {

// What an edit is to the expected set: the terminal it inserts, if any.
inline std::uint32_t ll_search_kind(const LlEdit& g) {
  return g.kind == LlEdit::ins ? std::uint32_t{g.term}
                               : std::uint32_t{kNoElement};
}

inline bool ll_search_goal(const LlReading& r) {
  // A choice may begin before the actual refusal. Its sound prefix does not
  // validate a repair: the three tokens must follow at least one edit. Arms
  // that still stand as far as a plan replay reads are not told apart here.
  return r.done || (r.faults != 0 && r.shifts >= kRepairValidateShifts &&
                    (r.decided || r.shifts >= kLlArmReach));
}

inline void ll_search_queue(LlSearch& s, std::uint32_t f, const LlReading& r,
                            std::uint32_t at, std::uint32_t lost = 0) {
  s.queue(s.nodes, f, r, at, lost,
          !r.generates() && !ll_search_goal(r) && r.fresh);
}

// Once an edit's readings are admitted, the tail no queue entry names is
// given back; where none is left, so is the pool they filled, and where none
// passed its link on, the links.
void ll_search_trim(LlSearch& s, const LlSearchCheckpoint& m);

// The fields of a reading its future depends on, besides its places and its
// levels: what `ll_search_future` folds and `ll_search_same_future` compares.
// The flags share one word, each in bits of its own.
inline std::array<std::uint64_t, 5> ll_search_scalars(const LlReading& r) {
  return {r.ahead.p, r.gapFrom, r.tokens, r.ahead.head,
          r.shifts | static_cast<std::uint64_t>(r.phase) << 8 |
              std::uint64_t{r.fresh} << 16 | std::uint64_t{r.done} << 17 |
              std::uint64_t{r.faults != 0} << 18 |
              std::uint64_t{r.decided} << 19 | std::uint64_t{r.inHand} << 20};
}

// What a reading's future depends on, besides the run it must still read: two
// readings with the same one gain the same key increments. Completed positions
// count: they decide whether a token is needed or optional.
template <const auto& T>
LlSearch::Future ll_search_future(const LlSearch& s, const LlReading& r) {
  LlSearch::Future f{0xCBF29CE484222325ull, 0x9E3779B97F4A7C15ull};
  const auto mix = [&](std::uint64_t v) {
    f.a = (f.a ^ v) * 0x100000001B3ull;
    f.a ^= f.a >> 29;
    f.b = (f.b + v + 0x7F4A7C15ull) * 0xBF58476D1CE4E5B9ull;
    f.b ^= f.b >> 31;
  };
  std::uint64_t last = ~std::uint64_t{0};
  for (std::uint32_t k = 0; k < r.ncfg; ++k) {
    const LlConfig& x = s.pool[r.cfg + k];
    const std::uint64_t place = std::uint64_t{x.state} << 32 | x.ctx;
    if (place == last) continue;
    last = place;
    mix(place);
  }
  for (const std::uint64_t v : ll_search_scalars(r)) mix(v);
  if constexpr (kIndented<T>) {
    // The levels by content: readings that made a level apart number it
    // apart.
    mix(std::uint64_t{r.ahead.held} << 32 | r.ahead.at.depth);
    for (std::uint32_t at = r.ahead.at.head; at != kNoIndent;
         at = s.indent.arena[at].prev) {
      const IndentLevel& level = s.indent.arena[at];
      mix(std::uint64_t{level.begin} << 32 | level.len);
      mix(std::uint64_t{level.virt} | std::uint64_t{level.mute} << 1);
    }
    mix(kNoIndent);
  }
  return f;
}

// Whether two readings have the same future: what `ll_search_future` folds,
// compared field by field.
template <const auto& T>
bool ll_search_same_future(const LlSearch& s, const LlReading& a,
                           const LlReading& b) {
  if (ll_search_scalars(a) != ll_search_scalars(b)) return false;
  // The places in order, each run of one place read as one.
  const auto place = [&](std::uint32_t k) {
    const LlConfig& x = s.pool[k];
    return std::uint64_t{x.state} << 32 | x.ctx;
  };
  std::uint32_t i = a.cfg, j = b.cfg;
  const std::uint32_t endA = a.cfg + a.ncfg, endB = b.cfg + b.ncfg;
  while (i != endA && j != endB) {
    const std::uint64_t at = place(i);
    if (place(j) != at) return false;
    while (i != endA && place(i) == at) ++i;
    while (j != endB && place(j) == at) ++j;
  }
  if (i != endA || j != endB) return false;
  if constexpr (kIndented<T>)
    return a.ahead.held == b.ahead.held &&
           indent_same(s.indent.arena, a.ahead.at, b.ahead.at);
  return true;
}

// Reading `i` queued, unless the one its future keeps outdoes it.
template <const auto& T>
void ll_search_admit(LlSearch& s, std::uint32_t i) {
  LlReading& r = s.nodes[i];
  const LlSearch::Future future = ll_search_future<T>(s, r);
  auto [it, last] = s.seen.equal_range(future);
  while (it != last && !ll_search_same_future<T>(s, r, s.nodes[it->second]))
    ++it;
  if (it == last)
    s.seen.emplace(future, i);
  else if (repair_merge<&LlReading::afterRequired>(s, s.nodes, i, it->second,
                                                   ll_search_kind))
    return;
  ll_search_queue(
      s, r.cost + (!ll_search_goal(r) && !r.afterRequired ? kRepairReread : 0),
      r, i);
}

inline void ll_search_load(LlSearch& s, const LlReading& r) {
  s.sim.load(s.pool.data() + r.cfg, r.ncfg);
  s.sim.at_token(r.tokens);
}

// The simulation's set, stored in the pool as the reading's own: the positions
// that can take a token or end their rule, the others being routes to those.
template <const auto& T>
void ll_search_keep(LlSearch& s, LlReading& r) {
  const LlRemaining<T>& remaining = ll_remaining<T>();
  r.cfg = static_cast<std::uint32_t>(s.pool.size());
  if (s.facts.cfg >= r.cfg) s.facts.cfg = kLlNoNode;
  std::uint16_t first = kNoRow;
  r.decided = true;
  for (const LlConfig& x : s.sim.configs())
    if (remaining.standing(x.state)) {
      s.pool.push_back(x);
      if (first == kNoRow) first = x.alt;
      r.decided = r.decided && first == x.alt;
    }
  r.ncfg = static_cast<std::uint32_t>(s.pool.size()) - r.cfg;
}

// Whether the search's share is spent, counting the walks since it last asked.
inline bool ll_search_spent(LlSearch& s) {
  const std::uint64_t units = (s.sim.visits() - s.visited) / kLlVisitsPerWork;
  s.work += units;
  s.visited += units * kLlVisitsPerWork;
  return s.work > s.limit;
}

inline void ll_search_cut(LlSearch& s) {
  if (s.work <= s.limit) s.work = s.limit + 1;
}

inline TextOffset ll_search_walk_end(const LlContext& c, const LlSearch& s,
                                     TextOffset p) {
  const std::uint64_t bytes = repair_bytes(s.work, s.limit);
  return c.len - p > bytes ? static_cast<TextOffset>(p + bytes) : c.len;
}

// A walk stopped at an artificial end answers nothing and is never cached.
inline bool ll_search_walked(const LlContext& c, LlSearch& s, TextOffset from,
                             TextOffset end, TextOffset reached,
                             TextOffset textEnd = kLlUnscanned) {
  s.work += (reached - from) / kRepairLexBytes;
  if (end >= std::min(c.len, textEnd) || reached < end) return false;
  ll_search_cut(s);
  return true;
}

// Guards read the gap and may look past their token. At an artificial end
// their answer is unknown: stop the search before it can retain that answer.
template <const auto& T>
bool ll_search_condition(const LlContext& c, LlSearch& s,
                          std::uint16_t condition, const Token& token,
                          TextOffset gapFrom) {
  const TextOffset from = std::min(gapFrom, token.begin);
  const TextOffset end = ll_search_walk_end(c, s, from);
  if (token.end > end) {
    ll_search_cut(s);
    return false;
  }
  c.stop.check();
  TextOffset reached = token.begin;
  const bool holds = ll_condition_text<T>(
      std::string_view(c.src, end), condition, token, gapFrom,
      ll_ends<T>(c, token.sym), &reached);
  ++s.work;
  return !ll_search_walked(c, s, from, end, reached) && holds;
}

// The positions loaded, past the conditions on the gap before `tok`.
template <const auto& T>
bool ll_search_filter(const LlContext& c, LlSearch& s, const Token& tok,
                      TextOffset gapFrom) {
  return s.sim.filter([&](std::uint16_t condition) {
    return ll_search_condition<T>(c, s, condition, tok, gapFrom);
  });
}

template <const auto& T>
std::vector<Scan>& ll_search_row(LlSearch& s, TextOffset p) {
  std::vector<Scan>& row = s.scans[p];
  if (row.empty()) row.assign(T.nmodes, Scan{kLlUnscanned, kInvalidSymbol, 0});
  return row;
}

template <const auto& T>
Scan ll_search_scan(const LlContext& c, LlSearch& s, std::vector<Scan>& row,
                    std::uint16_t mode, TextOffset p) {
  Scan& sc = row[mode];
  if (sc.end == kLlUnscanned) {
    c.stop.check();
    const TextOffset end = ll_search_walk_end(c, s, p);
    TextOffset reached = p;
    const Scan read = ll_match_text<T>(c.src, end, mode, p, reached);
    ++s.work;
    if (ll_search_walked(c, s, p, end, reached))
      return Scan{p, kInvalidSymbol};
    sc = read;
  }
  return sc;
}

// A section's closer is recognized only by complete tokens. Looking for it
// obeys the same share as the reading that needs the boundary: the bytes read
// are counted together, and a walk the share cut answers nothing.
template <const auto& T>
TextOffset ll_search_text_end(const LlContext& c, LlSearch& s, TextOffset at) {
  if constexpr (T.nsections != 0) {
    if (c.section != 0) {
      const TextOffset end = ll_search_walk_end(c, s, at);
      TextOffset reached = at;
      const TextOffset closes = ll_text_end<T>(c, at, end, reached, end);
      ++s.work;
      if (!ll_search_walked(c, s, at, end, reached) && closes == c.len &&
          end < c.len)
        ll_search_cut(s);
      return closes;
    }
  }
  return c.len;
}

// A read ahead of the descent whose levels stand in the search's arena: the
// descent's own, copied there.
template <const auto& T>
LlAhead ll_search_ahead(const LlContext& c, LlSearch& s) {
  LlAhead a = ll_ahead(c);
  if constexpr (kIndented<T>) {
    ll_indent_copy(c, s.indent);
    a.at = s.indent.at;
  }
  return a;
}

// The visible token, trivia crossed unless `cross` is false; only complete
// reads that no level changes enter the memo. An artificial end never makes EOF
// or a dedent. Without `cross`, the descent's own token is read.
template <const auto& T>
Token ll_search_peek_ahead(const LlContext& c, LlSearch& s, LlAhead& ahead,
                           std::uint16_t look,
                           const LlMask<T.termWords>& viable, bool cross) {
  if (!cross && c.la.sym < T.nterms && c.la.sym != kEofSymbol &&
      !T.mode_admits(T.errMode, c.la.sym))
    return c.la;
  const auto mode = ll_err_mode<T>(look != kNoRow ? look : c.laMode, c.section);
  TextOffset p = ahead.p;
  bool head = ahead.head;
  for (;;) {
    c.stop.poll();
    if (ll_search_spent(s)) return Token{p, p, kInvalidSymbol};
    if constexpr (kIndented<T>) {
      const TextOffset end = ll_search_walk_end(c, s, p);
      TextOffset reached = p;
      Token virtualToken;
      const bool found = ll_indent_read<T>(
          c.src, end, s.indent.arena, ahead.at, p, head, ahead.held,
          virtualToken, ll_mode_ends<T>(mode), &reached);
      if (ll_search_walked(c, s, p, end, reached))
        return Token{p, p, kInvalidSymbol};
      if (found) return virtualToken;
    }
    if (p >= c.len) return Token{p, p, kEofSymbol};
    if (cross && look != kNoRow) {
      const auto blanks =
          ll_skips_of<T>(look, kIndented<T> && ahead.at.depth != 0);
      const Scan blank = ll_search_scan<T>(c, s, ll_search_row<T>(s, p),
                                           blanks, p);
      if (ll_search_spent(s)) return Token{p, p, kInvalidSymbol};
      if (blank.term != kInvalidSymbol && blank.end > p) {
        const TextOffset cap = ll_search_walk_end(c, s, p);
        TextOffset reached = p;
        std::vector<std::uint16_t> visible;
        const TextOffset end =
            ll_match_in<T>(c.src, cap, p, viable, visible, &reached);
        ++s.work;
        if (ll_search_walked(c, s, p, cap, reached))
          return Token{p, p, kInvalidSymbol};
        if (end < blank.end) {
          p = blank.end;
          head = false;
          continue;
        }
      }
    }
    const Scan sc = ll_search_scan<T>(c, s, ll_search_row<T>(s, p), mode, p);
    if (ll_search_spent(s) || sc.term == kInvalidSymbol || sc.end == p)
      return Token{p, p, kInvalidSymbol};
    if (const bool blank =
            cross && look == kNoRow && ll_blank<T>(sc.term, ahead.at.depth);
        !blank)
      return Token{p, sc.end, sc.term, mode};
    p = sc.end;
    head = false;
  }
}

// A single skip configuration belongs to the active positions, even where
// there is no choice (and thus no lookMode table). Mixed configurations keep
// the broad reading; none may impose its trivia on the others.
template <const auto& T>
std::uint16_t ll_search_look(const LlConfig* cfg, std::size_t n,
                             LlMask<T.termWords>& viable, bool joined) {
  if constexpr (T.edgeNode == nullptr || T.modeTrivia == nullptr ||
                T.modeJoinedTrivia == nullptr)
    return kNoRow;
  const auto blanks = joined ? T.modeJoinedTrivia : T.modeTrivia;
  std::uint16_t mode = kNoRow;
  for (std::size_t k = 0; k < n; ++k) {
    const auto& state = T.atn.state(cfg[k].state);
    for (std::uint32_t j = 0; j < state.nedges; ++j) {
      using enum LlEdgeKind;
      const auto e = state.edge + j;
      const auto& edge = T.atn.edge(e);
      if (edge.kind != atom && edge.kind != condition) continue;
      if (edge.kind == atom && edge.label != kEofSymbol &&
          !(joined && T.terms[edge.label].joins))
        viable.set(edge.label);
      const auto hop = T.edgeNode[e];
      const auto own = hop != kNoRow ? T.nodes[hop].mode : T.tailMode;
      if (mode != kNoRow && blanks[mode] != blanks[own])
        return kNoRow;
      mode = own;
    }
  }
  return mode;
}

// The token in hand as the positions' alphabet reads it, as the descent does:
// the fault's alphabet may read a token over a blank they skip. The descent's
// own token is read where it begins, its gap being the point's.
template <const auto& T>
Token ll_search_peek(const LlContext& c, LlSearch& s, LlReading& r) {
  LlMask<T.termWords> viable;
  const bool joined = kIndented<T> && r.ahead.at.depth != 0;
  const auto look =
      ll_search_look<T>(s.pool.data() + r.cfg, r.ncfg, viable, joined);
  const bool memo = !kIndented<T> && look == kNoRow && !r.inHand;
  if (memo)
    if (const auto it = s.lexed.find(r.ahead.p); it != s.lexed.end())
      return it->second;
  const Token t =
      ll_search_peek_ahead<T>(c, s, r.ahead, look, viable, !r.inHand);
  if (memo && !ll_search_spent(s)) s.lexed.try_emplace(r.ahead.p, t);
  return t;
}

// All readings of the token's bytes in the positions' shared alphabet, as the
// descent reads them. A keyword reserves its spelling from a name while both
// are wanted; with only the name wanted, it is that name.
template <const auto& T>
void ll_search_terms(const LlContext& c, LlSearch& s, const LlConfig* cfg,
                     std::size_t n, Token& tok,
                     std::vector<std::uint16_t>& out) {
  out.clear();
  if (tok.sym < T.nterms && T.terms[tok.sym].synthetic != 0) {
    out.push_back(tok.sym);
    return;
  }
  LlMask<T.termWords> viable;
  bool any = false;
  bool broad = true;
  std::uint32_t last = ~0u;
  for (std::size_t k = 0; k < n && !ll_search_spent(s); ++k) {
    const std::uint32_t state = cfg[k].state;
    if (state == last) continue;
    last = state;
    for (const auto& a : ll_atoms<T>().at(state)) {
      viable.set(a.label);
      any = true;
      broad = broad && ll_err_mode<T>(a.mode, c.section) == tok.mode;
    }
  }
  if (!any || ll_search_spent(s)) return;
  // Under the alphabet a fault of the positions is lexed with, two terminals
  // never tie, and a wider one read this token: where they want it, no walk
  // under theirs reads otherwise.
  if (T.nrelays == 0 && !LexTape<T>::kRefuses && broad &&
      T.modeMulti[tok.mode] == 0 && tok.sym < T.nterms &&
      viable.test(tok.sym)) {
    out.push_back(tok.sym);
    ++s.work;
    return;
  }
  c.stop.check();
  const TextOffset cap = ll_search_walk_end(c, s, tok.begin);
  TextOffset reached = tok.begin;
  const TextOffset end =
      ll_match_in<T>(c.src, cap, tok.begin, viable, out, &reached);
  ++s.work;
  // A spelling only a condition names stays one token: it is no broad reading.
  if (ll_search_walked(c, s, tok.begin, cap, reached) ||
      (end != tok.end && tok.sym < T.nterms &&
       !T.mode_admits(T.errMode, tok.sym)))
    out.clear();
  else if (!out.empty())
    tok.end = end;
}

// How many calls deep a context stands, by its first return.
std::uint32_t ll_search_calls(LlSearch& s, std::uint32_t ctx);

// The author's real tokens read by actual call `frame` before this pause, of
// the frames the search stands on. Calls are nested, so one backward walk
// answers every frame it passes.
std::uint32_t ll_search_source_tokens(const LlContext& c, LlSearch& s,
                                      std::uint32_t frame, std::uint32_t most);

// What the positions of reading `i` offer, cached for the last reading asked:
// the reference holds until another is asked. Terminals come in repair order,
// the innermost call's first.
template <const auto& T>
const LlFacts& ll_search_facts(const LlContext& c, LlSearch& s,
                               std::uint32_t i) {
  const LlReading& r = s.nodes[i];
  LlFacts& f = s.facts;
  if (f.reading == i && f.cfg == r.cfg && f.n == r.ncfg) return f;
  f.reading = i;
  f.cfg = r.cfg;
  f.n = r.ncfg;
  ll_clear(f.viable, f.due, f.rest, f.modes, f.bits, f.written);
  ll_search_load(s, r);
  if constexpr (T.ngapConds != 0) {
    const Token tok = ll_search_peek<T>(c, s, s.nodes[i]);
    if (!ll_search_filter<T>(c, s, tok, r.gapFrom)) {
      if (ll_search_spent(s)) f.cfg = kLlNoNode;
      return f;
    }
  }
  std::uint32_t last = ~0u;
  for (const LlConfig& cfg : s.sim.configs()) {
    const std::uint32_t state = cfg.state;
    if (state == last) continue;
    last = state;
    const std::uint64_t place =
        std::uint64_t{0xFFFFFFFFu - ll_search_calls(s, cfg.ctx)}
            << 32 |
        state;
    for (const auto& a : ll_atoms<T>().at(state)) {
      const std::uint64_t bit = std::uint64_t{1} << (a.label & 63u);
      auto at = std::ranges::lower_bound(f.modes, a.mode);
      const auto m = static_cast<std::size_t>(at - f.modes.begin());
      if (at == f.modes.end() || *at != a.mode) {
        f.modes.insert(at, a.mode);
        f.bits.insert(
            f.bits.begin() + static_cast<std::ptrdiff_t>(m * T.termWords),
            T.termWords, 0);
      }
      f.bits[m * T.termWords + (a.label >> 6)] |= bit;
      f.written.emplace_back(place, a.label);
    }
  }
  std::ranges::sort(f.written);
  std::array<std::uint64_t, T.termWords> had{};
  for (const auto& [place, label] : f.written) {
    const std::uint64_t bit = std::uint64_t{1} << (label & 63u);
    if ((had[label >> 6] & bit) == 0) {
      had[label >> 6] |= bit;
      f.viable.push_back(label);
    }
  }
  f.key = 1469598103934665603ull;
  for (const std::uint16_t m : f.modes) f.key = (f.key ^ m) * 1099511628211ull;
  for (const std::uint64_t w : f.bits) f.key = (f.key ^ w) * 1099511628211ull;
  // Filtering keeps only ready positions, so with conditions the call positions
  // are loaded again to tell what an unfinished caller still needs. A call is
  // begun once anything under it read a token.
  if constexpr (T.ngapConds != 0) ll_search_load(s, r);
  const auto lowest =
      static_cast<std::uint32_t>(c.stack.size() - s.sourceTokens.size());
  ll_remaining<T>().due(s.sim, f.due, [&](std::uint32_t ctx) {
    if (ctx == kLlCtxNone) return r.tokens != 0;
    for (unsigned k = 0; k < s.sim.contexts().count(ctx); ++k) {
      const std::int64_t start = s.sim.contexts().start(ctx, k);
      if (start >= 0 ? r.tokens > static_cast<std::uint64_t>(start)
                     : r.tokens != 0 ||
                           ll_search_source_tokens(c, s, lowest, 0) != 0)
        return true;
    }
    return false;
  });
  std::erase_if(f.due, [&](std::uint16_t t) {
    return std::ranges::find(f.viable, t) == f.viable.end();
  });
  for (const std::uint16_t t : f.viable)
    if (std::ranges::find(f.due, t) == f.due.end()) f.rest.push_back(t);
  if (ll_search_spent(s)) f.cfg = kLlNoNode;
  return f;
}

// The token that closes the section the descent stands in: needed only where
// the document ends inside it.
template <const auto& T>
SymbolId ll_search_closer(const LlContext& c) {
  return T.nsections != 0 && c.section != 0 ? T.sectionEnd[c.section - 1u]
                                            : kInvalidSymbol;
}

template <const auto& T>
bool ll_search_synthetic(const Token& t) {
  return t.sym < T.nterms && T.terms[t.sym].synthetic != 0;
}

// The end of input while a level is open, as the drive reads it where the
// positions loaded stand: the end where they take it and no dedent, and the
// dedent first where they take one. A line's dedent never stands at the end.
template <const auto& T>
void ll_search_at_end(const LlContext& c, const LlSearch& s,
                      const LlAhead& ahead, Token& tok) {
  if constexpr (kIndented<T>) {
    if (tok.sym == kEofSymbol &&
        ll_end_dedents<T>(s.indent.arena, ahead.at, false) &&
        ll_dedent_over_end<T>(s.sim.configs()))
      tok.sym = kSyntheticTerm<T, kSyntheticDedent>;
    else if (kSyntheticTerm<T, kSyntheticDedent> != 0 &&
             tok.sym == kSyntheticTerm<T, kSyntheticDedent> &&
             tok.begin == c.len && ll_end_stands_over<T>(s.sim.configs()))
      tok.sym = kEofSymbol;
  }
}

// One token read by reading `i`: the conditions on its gap asked, the
// positions stepped by it. Refusal keeps the original positions: deleting
// this token must evaluate its guards again against the next one.
template <const auto& T>
RepairRead ll_search_step(LlContext& c, LlSearch& s, std::uint32_t i) {
  using enum RepairRead;
  if (ll_search_spent(s)) return spent;
  c.stop.poll();
  Token tok = ll_search_peek<T>(c, s, s.nodes[i]);
  if (ll_search_spent(s)) return spent;
  LlReading& r = s.nodes[i];
  if (tok.sym == kInvalidSymbol) return refused;
  s.work += 8;
  ll_search_load(s, r);
  ll_search_at_end<T>(c, s, r.ahead, tok);
  const bool ends = ll_ends<T>(c, tok.sym);
  bool passed = true;
  if constexpr (T.ngapConds != 0)
    passed = ll_search_filter<T>(c, s, tok, r.gapFrom);
  if (passed && !ends)
    ll_search_terms<T>(c, s, s.sim.configs().data(), s.sim.configs().size(),
                       tok, s.terms);
  if (ll_search_spent(s)) return spent;
  s.sim.at_token(r.tokens + (!ends && !ll_search_synthetic<T>(tok) ? 1 : 0));
  if (passed && (ends ? s.sim.step(tok.sym)
                      : s.sim.step(s.terms.data(), s.terms.size()))) {
    ll_search_keep<T>(s, r);
    if (ends) {
      r.done = true;
      return done;
    }
    r.gapFrom = tok.end;
    r.inHand = false;
    ll_ahead_step<T>(s.indent.arena, r.ahead, tok, false);
    if (!ll_search_synthetic<T>(tok)) {
      ++r.tokens;
      r.shifts = static_cast<std::uint8_t>(
          std::min<std::uint32_t>(r.shifts + 1, kLlArmReach));
      r.fresh = true;
      r.phase = RepairPhase::beforeEdit;
      r.afterRequired = false;
    }
    return shifted;
  }
  return refused;
}

// Reading `i` reads until it is refused, holds or ends: true where its share
// of work leaves it standing.
template <const auto& T>
bool ll_search_read(LlContext& c, LlSearch& s, std::uint32_t i) {
  for (;;) {
    if (ll_search_goal(s.nodes[i])) break;
    const RepairRead read = ll_search_step<T>(c, s, i);
    if (read == RepairRead::spent) return false;
    if (read != RepairRead::shifted) break;
  }
  return true;
}

template <const auto& T>
void ll_search_add(LlContext& c, LlSearch& s, std::uint32_t e) {
  if (ll_search_read<T>(c, s, e)) ll_search_admit<T>(s, e);
}

std::uint32_t ll_search_child(LlSearch& s, std::uint32_t from,
                              LlEdit::Kind kind, SymbolId term, TextOffset end,
                              std::uint32_t choice, TextOffset begin);

// A terminal the readings may invent in front of the token in hand: no implied
// one, nor the one that closes a section but at its end.
template <const auto& T>
bool ll_search_insertable(const LlContext& c, std::uint16_t term, bool atEnd) {
  return term != kEofSymbol && T.terms[term].synthetic != kSyntheticImplied &&
         (atEnd || term != ll_search_closer<T>(c));
}

// The terminals reading `bi` offers an insertion, the ones its shortest ways
// out begin with first and the others as they were declared, each with its
// place in that order. `visit` answers whether to go on.
template <const auto& T, class Visit>
void ll_search_each_insert(const LlContext& c, LlSearch& s,
                            std::uint32_t bi, Visit&& visit) {
  const LlFacts& f = ll_search_facts<T>(c, s, bi);
  unsigned rank = 0;
  for (const std::uint16_t t : f.due)
    if (!visit(t, rank++, true)) return;
  for (const std::uint16_t t : f.rest)
    if (!visit(t, rank++, false)) return;
}

// A terminal invented in front of the token in hand: the positions that read
// it carry on, and a required one that follows a required one costs nothing.
// Its readings are admitted, and what they leave is given back.
template <const auto& T>
void ll_search_insert(LlContext& c, LlSearch& s, std::uint32_t bi,
                      std::uint16_t term, std::uint32_t choice, bool required,
                      TextOffset begin) {
  const LlSearchCheckpoint pools(s);
  const LlReading b = s.nodes[bi];
  ll_search_load(s, b);
  if constexpr (T.ngapConds != 0) {
    const Token tok = ll_search_peek<T>(c, s, s.nodes[bi]);
    if (ll_search_spent(s) || !ll_search_filter<T>(c, s, tok, b.gapFrom))
      return;
  }
  if (!s.sim.step(term)) return;
  const std::uint32_t e =
      ll_search_child(s, bi, LlEdit::ins, term, 0, choice, begin);
  LlReading& r = s.nodes[e];
  ll_search_keep<T>(s, r);
  const bool free = required && b.afterRequired;
  r.cost += free ? 0 : kRepairEdit;
  r.edits -= free ? 1u : 0u;
  r.afterRequired = required;
  r.invented += 1;
  r.phase = RepairPhase::inserting;
  // A level the token moves has the line read again from the gap, as the
  // descent reads it.
  if constexpr (kIndented<T>)
    if (indent_moves<T>(term)) {
      indent_shift_invented<T>(s.indent.arena, r.ahead.at,
                               Token{b.gapFrom, b.gapFrom, term});
      r.ahead.p = b.gapFrom;
      r.ahead.head = true;
      r.inHand = false;
    }
  ll_search_add<T>(c, s, e);
  ll_search_trim(s, pools);
}

// The insertions that are not required, made when the entry that represents
// them is asked for.
template <const auto& T>
void ll_search_inserts(LlContext& c, LlSearch& s, std::uint32_t gi) {
  const std::uint32_t from = s.nodes[gi].genFrom;
  const std::uint32_t rank = s.nodes[gi].genKeep;
  const Token tok = ll_search_peek<T>(c, s, s.nodes[from]);
  if (ll_search_spent(s)) return;
  const bool atEnd = ll_ends<T>(c, tok.sym);
  ll_search_each_insert<T>(
      c, s, from, [&](std::uint16_t t, unsigned k, bool required) {
        if (ll_search_spent(s)) return false;
        if (required || !ll_search_insertable<T>(c, t, atEnd)) return true;
        ll_search_insert<T>(c, s, from, t, rank + k, false, tok.begin);
        return true;
      });
}

// The lexer's refused reading of the token at `p` under `mode`, each once.
template <const auto& T>
const LexReading& ll_search_reading(LlContext& c, LlSearch& s, TextOffset p,
                                    std::uint16_t mode) {
  static constexpr LexReading kNone{};
  if (p >= c.len ||
      !T.lexOpeners[mode].has(static_cast<unsigned char>(c.src[p])))
    return kNone;
  const std::uint64_t key = std::uint64_t{p} << 16 | mode;
  if (const auto it = s.readings.find(key); it != s.readings.end())
    return it->second;
  const TextOffset textEnd = ll_search_text_end<T>(c, s, p);
  if (ll_search_spent(s)) return kNone;
  const TextOffset end = std::min(textEnd, ll_search_walk_end(c, s, p));
  TextOffset reached = p;
  c.stop.check();
  const LexReading r =
      lex_reading(LlLexTape<T>{}, c.src, end, mode, p, reached);
  ++s.work;
  if (ll_search_walked(c, s, p, end, reached, textEnd)) return kNone;
  return s.readings.try_emplace(key, r).first->second;
}

// The alphabet reading `r`'s token is read under for what the lexer refused:
// its positions' own where they share one, as the descent reads it. Under the
// fault's, a trivia another region reads as a token is that token.
template <const auto& T>
std::uint16_t ll_search_lex_mode(const LlContext& c, const LlSearch& s,
                                 const LlReading& r) {
  LlMask<T.termWords> viable;
  const auto look =
      ll_search_look<T>(s.pool.data() + r.cfg, r.ncfg, viable,
                        kIndented<T> && r.ahead.at.depth != 0);
  return look != kNoRow ? look : ll_err_mode<T>(c.laMode, c.section);
}

// The token in hand read as `term` up to `end` at `cost`: `rep` where its
// bytes are re-read as a terminal the positions step by, `skip` where they are
// a trivia the lexer refused. The resulting reading, or `kLlNoNode`.
template <const auto& T>
std::uint32_t ll_search_reread(LlContext& c, LlSearch& s, std::uint32_t bi,
                               LlEdit::Kind kind, SymbolId term, TextOffset end,
                               std::uint32_t choice, const Token& tok,
                               std::uint32_t cost) {
  const std::uint32_t e =
      ll_search_child(s, bi, kind, term, end, choice, tok.begin);
  LlReading& r = s.nodes[e];
  r.cost += cost;
  r.afterRequired = false;
  const Token read{tok.begin, end, term};
  if (kind == LlEdit::skip) {
    r.phase = RepairPhase::discardOnly;
    r.gapFrom = end;
    ll_ahead_step<T>(s.indent.arena, r.ahead, read, true);
  } else {
    ll_search_load(s, r);
    if constexpr (T.ngapConds != 0)
      if (!ll_search_filter<T>(c, s, read, r.gapFrom)) return kLlNoNode;
    s.sim.at_token(r.tokens + 1);
    if (!s.sim.step(term)) return kLlNoNode;
    ++r.tokens;
    ll_search_keep<T>(s, r);
    r.gapFrom = end;
    ll_ahead_step<T>(s.indent.arena, r.ahead, read, false);
    r.fresh = true;
    r.phase = RepairPhase::beforeEdit;
  }
  return ll_search_read<T>(c, s, e) ? e : kLlNoNode;
}

// The token in hand as the lexer's refused reading of it, ending where it
// closes, at each line end that cuts no word, or at the text's end; trivia so
// read is skipped. The ends are found once per search.
template <const auto& T>
void ll_search_lexical(LlContext& c, LlSearch& s, std::uint32_t bi,
                       const Token& tok, std::uint32_t& choice) {
  const std::uint16_t mode = ll_search_lex_mode<T>(c, s, s.nodes[bi]);
  const LexReading r = ll_search_reading<T>(c, s, tok.begin, mode);
  if (r.term == kInvalidSymbol || r.term == ll_search_closer<T>(c)) return;
  const bool trivia = T.terms[r.term].trivia;
  if (!trivia) {
    const std::vector<std::uint16_t>& viable = ll_search_facts<T>(c, s, bi).viable;
    if (!s.nodes[bi].mayReplace() ||
        std::ranges::find(viable, r.term) == viable.end())
      return;
  }
  const auto one = [&](TextOffset end) {
    const LlSearchCheckpoint pools(s);
    const std::uint32_t q =
        ll_search_reread<T>(c, s, bi, trivia ? LlEdit::skip : LlEdit::rep,
                            r.term, end, choice++, tok, kRepairReread);
    bool holds = false;
    if (q != kLlNoNode) {
      holds = ll_search_goal(s.nodes[q]);
      ll_search_admit<T>(s, q);
    }
    ll_search_trim(s, pools);
    return holds;
  };
  if (r.closed) {
    one(r.end);
    return;
  }
  RepairEnds& ends = s.ends[std::uint64_t{tok.begin} << 16 | mode];
  const auto cuts = [&](TextOffset x) { return lex_cuts<T>(c.src, c.len, x); };
  for (std::size_t k = 0; !ll_search_spent(s); ++k) {
    if (k == ends.found.size()) {
      const TextOffset from = ends.regime == RepairEnds::none
                                  ? tok.begin : ends.scanned;
      bool cut = false;
      if (!ends.more(c.src, c.len, tok.begin, r, s.work, cuts,
                     ll_search_walk_end(c, s, std::min(from, c.len)), cut,
                     c.stop, s.limit)) {
        if (cut) ll_search_cut(s);
        return;
      }
    }
    if (one(ends.found[k]) && ends.regime == RepairEnds::lines) return;
  }
}

// The words reading `bi` may misspell at the token, ranked: for each alphabet
// its positions read next under, the terminals they read in it. Handed out
// once per search for each set of terminals.
template <const auto& T>
const std::vector<Typo>& ll_search_typos(LlContext& c, LlSearch& s,
                                         std::uint32_t bi, const Token& tok) {
  const LlFacts& f = ll_search_facts<T>(c, s, bi);
  const std::uint64_t key = (f.key ^ tok.begin) * 1099511628211ull;
  for (auto [it, last] = s.typos.equal_range(key); it != last; ++it)
    if (it->second.begin == tok.begin && it->second.modes == f.modes &&
        it->second.bits == f.bits)
      return it->second.typos;
  TypoCandidates found;
  const TextOffset len = ll_search_text_end<T>(c, s, tok.begin);
  const std::uint64_t most = repair_bytes(s.work, s.limit);
  const auto reach = static_cast<TextOffset>(
      std::min<std::uint64_t>(most, kLlUnscanned));
  const TextOffset horizon = typo_horizon(tok.begin, tok.end, len, reach);
  std::uint64_t read = 0;
  bool cut = ll_search_spent(s);
  for (std::size_t m = 0; m < f.modes.size() && !cut; ++m)
    ll_rescan_within<T>(c, f.modes[m], f.bits.data() + m * T.termWords,
                        tok.begin, tok.end, len, horizon, read, found, most,
                        cut, &s.typoScratch);
  s.work += 1 + read / kRepairLexBytes;
  if (cut) {
    ll_search_cut(s);
    s.cutTypos = found.ranked();
    return s.cutTypos;
  }
  return s.typos
      .emplace(key, LlSearch::TypoMemo{tok.begin, f.modes, f.bits,
                                       found.ranked()})
      ->second.typos;
}

// What a deletion of `tok` takes on reading `bi`: of a token left open, the
// opener its positions' alphabet reads, else the fault's.
template <const auto& T>
TextOffset ll_search_drop_end(LlContext& c, LlSearch& s, std::uint32_t bi,
                              const Token& tok) {
  if (tok.end != tok.begin) return tok.end;
  const std::uint16_t own = ll_search_lex_mode<T>(c, s, s.nodes[bi]);
  const std::uint64_t key = std::uint64_t{tok.begin} << 16 | own;
  if (const auto it = s.drops.find(key); it != s.drops.end())
    return it->second;
  for (const std::uint16_t mode : {own, ll_err_mode<T>(c.laMode, c.section)}) {
    const LexReading r = c.reads.spent()
                             ? LexReading{}
                             : ll_search_reading<T>(c, s, tok.begin, mode);
    if (ll_search_spent(s)) return tok.begin;
    if (r.term != kInvalidSymbol)
      return s.drops.emplace(key, lex_cut(tok.begin, r)).first->second;
  }
  const TextOffset cap = ll_search_walk_end(c, s, tok.begin);
  TextOffset reached = tok.begin;
  const TextOffset end = ll_unmatched_end<T>(c, tok.begin, cap, reached);
  ++s.work;
  if (!ll_search_walked(c, s, tok.begin, cap, reached))
    s.drops.emplace(key, end);
  return end;
}

// Whether refused reading `bi` may go on by a re-reading of the token in hand.
template <const auto& T>
bool ll_search_rereads(LlContext& c, LlSearch& s, std::uint32_t bi,
                       const Token& tok) {
  if (ll_ends<T>(c, tok.sym) || ll_search_synthetic<T>(tok)) return false;
  if (ll_search_reading<T>(c, s, tok.begin,
                           ll_search_lex_mode<T>(c, s, s.nodes[bi]))
          .term != kInvalidSymbol)
    return true;
  return (s.nodes[bi].mayReplace() &&
          !ll_search_typos<T>(c, s, bi, tok).empty()) ||
         ll_search_spent(s);
}

// Count and make an abandon only when the queue reaches what it costs. A
// large unfinished call must not spend a cheap repair's share counting its
// entire prefix first.
template <const auto& T>
void ll_search_abandon(LlContext& c, LlSearch& s, std::uint32_t gi,
                        std::uint32_t f) {
  const LlReading g = s.nodes[gi];
  const LlReading b = s.nodes[g.genFrom];
  const auto frame = static_cast<std::uint32_t>(-1 - g.genStart);
  const std::uint32_t most = (f - b.cost) / kRepairEdit;
  const std::uint32_t lost =
      g.genStart >= 0
          ? b.tokens - static_cast<std::uint32_t>(g.genStart)
          : b.tokens + ll_search_source_tokens(
                           c, s, frame, most > b.tokens ? most - b.tokens : 0);
  if (lost == 0 || ll_search_spent(s)) return;
  const std::uint32_t cost = b.cost + lost * kRepairEdit;
  if (cost > f) {
    LlReading& r = s.nodes[gi];
    r.cost = cost;
    r.lost = b.lost + lost;
    ll_search_queue(s, cost, r, gi);
    return;
  }
  const LlSearchCheckpoint pools(s);
  s.sim.at_token(b.tokens);
  s.sim.seed(g.genRet, g.genParent);
  std::vector<LlConfig> resumed(s.sim.configs());
  for (LlConfig& x : resumed) x.alt = g.genAlt;
  s.sim.load(resumed);
  const Token tok = ll_search_peek<T>(c, s, s.nodes[g.genFrom]);
  if (ll_search_spent(s)) return;
  const std::uint32_t e =
      ll_search_child(s, g.genFrom, LlEdit::abandon, g.genMissing, g.genKeep,
                      g.genChoice, tok.begin);
  s.links[s.nodes[e].link].state = g.genRet;
  LlReading& r = s.nodes[e];
  ll_search_keep<T>(s, r);
  r.cost = cost;
  r.lost += lost;
  r.phase = RepairPhase::afterAbandon;
  r.afterRequired = false;
  if (const RepairRead read = ll_search_step<T>(c, s, e);
      read == RepairRead::shifted || read == RepairRead::done)
    ll_search_add<T>(c, s, e);
  ll_search_trim(s, pools);
}

// Leaves a partially read call, keeping its exact continuation. Each (context,
// arm) is left once, from the least depth the walk reaches it at.
template <const auto& T>
void ll_search_abandons(const LlContext& c, LlSearch& s, std::uint32_t bi,
                         std::uint32_t& choice) {
  const LlReading b = s.nodes[bi];
  if (!b.mayAbandon()) return;
  const std::vector<std::uint16_t> due = ll_search_facts<T>(c, s, bi).due;
  ll_search_load(s, b);
  SymbolId missing = ll_remaining<T>().first(s.sim);
  if (missing == kNoElement && due.size() == 1) missing = due.front();
  struct At {
    std::uint32_t ctx, depth;
    std::uint16_t alt;
    bool operator==(const At& o) const { return ctx == o.ctx && alt == o.alt; }
  };
  struct AtHash {
    std::size_t operator()(const At& a) const {
      return std::hash<std::uint64_t>{}(std::uint64_t{a.ctx} << 16 ^ a.alt);
    }
  };
  // A deep nest's callers are many: the visited set is not searched linearly.
  std::vector<At> todo;
  std::unordered_set<At, AtHash> seen;
  const auto visit = [&](const At& at) {
    if (seen.insert(at).second) todo.push_back(at);
  };
  for (std::uint32_t k = 0; k < b.ncfg; ++k) {
    const LlConfig& x = s.pool[b.cfg + k];
    visit(At{x.ctx, 1, x.alt});
  }
  for (std::size_t k = 0; k < todo.size() && !ll_search_spent(s); ++k) {
    const At at = todo[k];
    if (at.ctx == kLlCtxNone) continue;
    const unsigned n = s.sim.contexts().count(at.ctx);
    for (unsigned j = 0; j < n && !ll_search_spent(s); ++j) {
      ++s.work;
      const std::uint32_t ret = s.sim.contexts().ret(at.ctx, j);
      const std::uint32_t parent = s.sim.contexts().parent(at.ctx, j);
      const std::int64_t start = s.sim.contexts().start(at.ctx, j);
      visit(At{parent, at.depth + 1, at.alt});
      const std::uint32_t lost = start >= 0
                                    ? b.tokens - static_cast<std::uint32_t>(start)
                                    : std::max(b.tokens, 1u);
      if (lost == 0) continue;
      LlReading g = b;
      g.work = RepairWork::enumerateAbandons;
      g.genFrom = bi;
      g.genKeep = at.depth;
      g.genRet = ret;
      g.genParent = parent;
      g.genStart = start;
      g.genAlt = at.alt;
      g.genMissing = missing;
      g.genChoice = choice++;
      g.lost += lost;
      s.defer(s.nodes, g, lost * kRepairEdit);
    }
  }
}

// The keyword the positions of reading `bi` read alone: a word in its place
// stands for it, whatever it looks like. None where they admit more.
template <const auto& T>
SymbolId ll_search_forced(const LlContext& c, LlSearch& s, std::uint32_t bi) {
  const LlFacts& f = ll_search_facts<T>(c, s, bi);
  if (f.viable.size() != 1) return kInvalidSymbol;
  const std::uint16_t t = f.viable.front();
  if (!T.terms[t].spelled || !lex_boundary(T).word_spelled(T.term_text(t)) ||
      !ll_search_insertable<T>(c, t, false))
    return kInvalidSymbol;
  return t;
}

// The word in hand read as the keyword its positions are forced to, over its
// deletion's span: one edit costed as a deletion and an insertion, or lower
// where the misspelling walk reads it there.
template <const auto& T>
void ll_search_replace(LlContext& c, LlSearch& s, std::uint32_t gi) {
  const std::uint32_t from = s.nodes[gi].genFrom;
  const Token tok = ll_search_peek<T>(c, s, s.nodes[from]);
  if (ll_search_spent(s)) return;
  const SymbolId term = ll_search_forced<T>(c, s, from);
  if (term == kInvalidSymbol) return;
  const TextOffset end = ll_search_drop_end<T>(c, s, from, tok);
  for (const Typo& t : ll_search_typos<T>(c, s, from, tok))
    if (t.end == end && t.term == term) return;
  // The alphabet the keyword is read under, as the walk reads it.
  const LlFacts& f = ll_search_facts<T>(c, s, from);
  std::uint16_t start = kDeadState;
  for (std::size_t m = 0; m < f.modes.size() && start == kDeadState; ++m)
    if (((f.bits[m * T.termWords + (term >> 6)] >> (term & 63u)) & 1u) != 0)
      start = T.modeStart[f.modes[m]];
  // One spelling held against the table: no walk, so nothing to count.
  if (const TypoTape<T> tape{
          {.src = c.src, .extent = c.len, .docLen = c.len, .read = nullptr},
          tok.begin};
      start == kDeadState || ll_search_spent(s) ||
      !tape.replaces(start, term, end))
    return;
  const LlSearchCheckpoint pools(s);
  if (const auto q =
          ll_search_reread<T>(c, s, from, LlEdit::rep, term, end,
                              s.nodes[gi].genKeep, tok, kRepairReplace);
      q != kLlNoNode)
    ll_search_admit<T>(s, q);
  ll_search_trim(s, pools);
}

// Whether no alphabet of the text reads a token at `p`, as one unit of bytes
// tells: a position an insertion reaches may read what the fault's
// configuration does not.
template <const auto& T>
bool ll_search_unread([[maybe_unused]] const LlContext& c,
                      [[maybe_unused]] LlSearch& s,
                      [[maybe_unused]] TextOffset p) {
  if constexpr (T.errModeOf == nullptr) {
    return true;
  } else {
    std::uint16_t mode = T.errMode;
    if constexpr (T.nsections != 0)
      if (c.section != 0) mode = ll_section_mode<T>(c.section);
    const TextOffset near =
        c.len - p > kRepairLexBytes ? p + kRepairLexBytes : c.len;
    TextOffset halt = p;
    ++s.work;
    return ll_match_text<T>(c.src, near, mode, p, halt).term ==
               kInvalidSymbol &&
           (halt < near || near == c.len);
  }
}

// The edits the canonical form admits, in rank order: re-readings, required
// insertions, the others, the drop.
template <const auto& T>
void ll_search_expand(LlContext& c, LlSearch& s, std::uint32_t bi) {
  const LlReading b = s.nodes[bi];
  const Token tok = ll_search_peek<T>(c, s, s.nodes[bi]);
  if (ll_search_spent(s)) return;
  const bool atEnd = ll_ends<T>(c, tok.sym);
  const bool synthetic = ll_search_synthetic<T>(tok);
  std::uint32_t choice = 0;
  if (!atEnd && !synthetic) ll_search_lexical<T>(c, s, bi, tok, choice);
  if (b.mayReplace() && !atEnd && !synthetic) {
    for (const Typo& t : ll_search_typos<T>(c, s, bi, tok)) {
      if (ll_search_spent(s)) break;
      if (t.term == ll_search_closer<T>(c)) continue;
      const LlSearchCheckpoint pools(s);
      if (const auto q = ll_search_reread<T>(c, s, bi, LlEdit::rep, t.term,
                                             t.end, choice++, tok, t.cost);
          q != kLlNoNode)
        ll_search_admit<T>(s, q);
      ll_search_trim(s, pools);
    }
  }
  // A word in place of the keyword its positions are forced to, unlike it,
  // waits behind one entry, at the cost of its deletion and the keyword's
  // insertion.
  if (b.mayReplace() && !atEnd && !synthetic && tok.sym != kInvalidSymbol &&
      lex_boundary(T).word_at(c.src, tok.begin, c.len) &&
      ll_search_forced<T>(c, s, bi) != kInvalidSymbol) {
    LlReading g = b;
    g.work = RepairWork::forcedReplacement;
    g.genFrom = bi;
    g.genKeep = choice;
    s.defer(s.nodes, g, kRepairReplace);
  }
  ++choice;
  // Bytes no alphabet reads refuse every reading the same: insertions in front
  // of them are told apart only past them, so one is made there, never a run.
  if (const bool garbage = !atEnd && !synthetic && tok.sym == kInvalidSymbol &&
                           ll_search_unread<T>(c, s, tok.begin);
      b.mayInsert(garbage)) {
    // Only a required run is free past its first, so the other terminals wait
    // behind one entry until a reading that costly is asked for.
    const std::uint32_t insertRank = choice;
    bool deferred = false;
    ll_search_each_insert<T>(
        c, s, bi, [&](std::uint16_t t, unsigned k, bool required) {
          if (!ll_search_insertable<T>(c, t, atEnd)) return true;
          if (ll_search_spent(s)) return false;
          if (!required) {
            deferred = true;
            return true;
          }
          ll_search_insert<T>(c, s, bi, t, insertRank + k, true, tok.begin);
          return true;
        });
    // Ranks need order, not consecutive values: two terminal-sized ranges put
    // required insertions first, then all others in declaration order.
    choice += 2u * T.nterms;
    if (deferred) {
      LlReading g = b;
      g.work = RepairWork::enumerateInsertions;
      g.genFrom = bi;
      g.genKeep = insertRank + T.nterms;
      g.invented += 1;
      s.defer(s.nodes, g, kRepairEdit);
    }
  }
  if (!atEnd) {
    const TextOffset end =
        synthetic ? tok.end : ll_search_drop_end<T>(c, s, bi, tok);
    const LlSearchCheckpoint pools(s);
    const std::uint32_t e =
        ll_search_child(s, bi, LlEdit::del, 0, end, choice++, tok.begin);
    LlReading& r = s.nodes[e];
    r.cost += kRepairEdit;
    r.lost += synthetic ? 0u : 1u;
    r.phase = RepairPhase::discardOnly;
    r.afterRequired = false;
    r.gapFrom = end;
    ll_ahead_step<T>(s.indent.arena, r.ahead, Token{tok.begin, end, tok.sym},
                     true);
    ll_search_add<T>(c, s, e);
    ll_search_trim(s, pools);
  }
  ll_search_abandons<T>(c, s, bi, choice);
}

// A race's step of the simulation: one token of a reading. A race costs
// nothing, so a reading holds its place in the text in `tokens`. A refusal
// stands past the blanks the indentation reads there.
template <const auto& T>
struct LlSearchRace {
  static constexpr std::uint32_t kSteps = kRepairRaceSteps;
  LlContext& c;
  LlSearch& s;
  std::unordered_multimap<LlSearch::Future, std::uint32_t, LlSearch::FutureHash>
      waiting{};
  RepairRead read = RepairRead::shifted;

  static TextOffset pos(const LlReading& r) { return r.ahead.p; }
  bool spent() const { return ll_search_spent(s); }
  std::uint32_t step(std::uint32_t i, std::vector<std::uint32_t>&) {
    LlReading& r = s.nodes[i];
    for (auto [it, last] = waiting.equal_range(ll_search_future<T>(s, r));
         it != last; ++it)
      if (it->second == i) {
        waiting.erase(it);
        break;
      }
    r.shifts = 0;
    r.tokens = r.ahead.p;
    read = ll_search_step<T>(c, s, i);
    r.tokens = r.ahead.p;
    return 1;
  }
  RepairRead stands(std::uint32_t) const { return read; }
  TextOffset refused(std::uint32_t i) const {
    const TextOffset at = ll_search_peek<T>(c, s, s.nodes[i]).begin;
    if constexpr (kIndented<T>) return indent_run(c.src, at, c.len).stop;
    return at;
  }
  std::uint32_t join(std::uint32_t i) {
    const LlSearch::Future future = ll_search_future<T>(s, s.nodes[i]);
    auto [it, last] = waiting.equal_range(future);
    while (it != last &&
           !ll_search_same_future<T>(s, s.nodes[i], s.nodes[it->second]))
      ++it;
    return it != last ? it->second : waiting.emplace(future, i)->second;
  }
};

// Two paths that make the same edits are one candidate.
std::uint64_t ll_edits_fold(const std::vector<LlEdit>& links, std::uint32_t l);

bool ll_same_edits(const std::vector<LlEdit>& links, std::uint32_t a,
                   std::uint32_t b);

// A plan names the pool of the search that made it: a search that makes
// another drops it.
void ll_search_reset(LlSearch& s);

// With no goal before the work ran out, the reading that went past the most
// of the author's tokens, read or dropped; of two that tie, the one that got
// furthest.
template <const auto& T>
std::uint32_t ll_search_run(LlContext& c, LlSearch& s, bool& exhausted,
                            bool& raced) {
  std::uint32_t furthest = kLlNoNode;
  std::uint32_t most = 0;
  exhausted = raced = false;
  while (!s.heap.empty()) {
    c.stop.poll();
    const RepairEntry e = s.pop();
    if (s.nodes[e.at].gone) continue;
    if (!s.goals.empty() && e.f > s.nodes[s.goals[0]].cost) break;
    if (ll_search_spent(s)) {
      exhausted = true;
      break;
    }
    switch (s.nodes[e.at].work) {
      using enum RepairWork;
      case read:
        break;
      case enumerateInsertions:
        ll_search_inserts<T>(c, s, e.at);
        continue;
      case forcedReplacement:
        ll_search_replace<T>(c, s, e.at);
        continue;
      case enumerateAbandons:
        ll_search_abandon<T>(c, s, e.at, e.f);
        continue;
    }
    if (ll_search_goal(s.nodes[e.at])) {
      s.goals.push_back(e.at);
      continue;
    }
    if (const LlReading& x = s.nodes[e.at];
        furthest == kLlNoNode ||
        std::pair{x.tokens + s.links[x.link].dropped, x.gapFrom} >
            std::pair{most, s.nodes[furthest].gapFrom}) {
      furthest = e.at;
      most = x.tokens + s.links[x.link].dropped;
    }
    // A refused reading needs at least one re-reading, or a whole edit
    // where none can take its token. Future typos can change both a token and
    // its successors, so they cannot raise this bound. Required runs stay free.
    if (!s.nodes[e.at].afterRequired &&
        e.f < s.nodes[e.at].cost + kRepairEdit &&
        !ll_search_rereads<T>(c, s, e.at,
                              ll_search_peek<T>(c, s, s.nodes[e.at]))) {
      ll_search_queue(s, s.nodes[e.at].cost + kRepairEdit, s.nodes[e.at], e.at,
                      e.lost - s.nodes[e.at].lost);
      continue;
    }
    ll_search_expand<T>(c, s, e.at);
  }
  // A walk the share cut leaves the goals of its cost unread: the share
  // decided, even where the bucket emptied.
  exhausted = exhausted || ll_search_spent(s);
  if (s.goals.empty()) return furthest;
  return repair_elect<ll_edits_fold, ll_same_edits, ll_search_kind>(
      s, s.nodes, false, c.stop, [&](const auto& of, auto& reach) {
        raced =
            repair_race(s, s.nodes, of, reach, c.stop, LlSearchRace<T>{c, s});
      });
}

void ll_search_plan(LlSearch& s, std::uint32_t ci);

// The simulation opened over the context of the descent: the top `frames`
// frames of its stack, below them an unknown caller.
std::uint32_t ll_search_open(const LlContext& c, LlSimulation& sim,
                             std::size_t frames);

// The root reading: the positions the descent stands in, and the text from
// the token in hand.
template <const auto& T>
std::uint32_t ll_search_root(LlContext& c, LlSearch& s, const LlRoot& root,
                             std::size_t frames) {
  LlSimulation& sim = s.sim;
  s.sourceTokens.assign(std::min(frames, c.stack.size()), kLlNoNode);
  s.sourceScanned = c.tape.stream().records();
  const std::uint32_t ctx = ll_search_open(c, sim, frames);
  std::vector<LlConfig> roots;
  if (root.decision != kNoRow) {
    sim.start(T.decisions[root.decision].state, ctx);
    roots.assign(sim.configs().begin(), sim.configs().end());
  } else {
    for (const auto& [node, state] : ll_atoms<T>().reading(root.node)) {
      sim.seed(state, ctx);
      roots.insert(roots.end(), sim.configs().begin(), sim.configs().end());
    }
    std::ranges::sort(roots);
    roots.erase(std::ranges::unique(roots).begin(), roots.end());
  }
  sim.load(roots);
  LlReading r;
  ll_search_keep<T>(s, r);
  r.ahead = ll_search_ahead<T>(c, s);
  r.inHand = true;
  r.gapFrom = c.at;
  r.link = s.link(LlEdit{});
  s.nodes.push_back(std::move(r));
  return static_cast<std::uint32_t>(s.nodes.size()) - 1;
}

template <const auto& T>
LlSearch& ll_search_of(LlContext& c) {
  if (!c.search) {
    c.search = std::make_unique<LlSearch>(T.atn, c.len, c.budget);
    c.search->sim.watch(c.stop);
  }
  return *c.search;
}

// The span a bound decided: the token in hand, or the rest of the text where
// the parse's share is spent.
template <const auto& T>
void ll_search_bounded(LlContext& c, bool whole) {
  ll_note_run<T>(c);
  c.tape.note_repair(DiagnosticKind::bounded, kNoElement, c.at,
                     whole ? c.len : std::max(c.at, c.la.end));
}

// The arm of `decision` the plan's path enters from here: the path replayed
// over that choice and the text after it until one arm stands, the lowest if
// several do. The plan's next edit is the one at this pause.
template <const auto& T>
std::uint16_t ll_search_arm_of(LlContext& c, LlSearch& s,
                               std::uint16_t decision) {
  LlSimulation& sim = s.sim;
  sim.start(T.decisions[decision].state, ll_search_open(c, sim, s.frames));
  LlAhead ahead = ll_search_ahead<T>(c, s);
  TextOffset gap = c.at;
  std::uint16_t arm = kNoRow;
  std::size_t k = s.next;
  std::uint32_t shifts = 0;
  std::uint32_t tokens = 0;
  bool inHand = true;
  for (bool first = true;; first = false) {
    LlMask<T.termWords> viable;
    const bool joined = kIndented<T> && ahead.at.depth != 0;
    const auto look = ll_search_look<T>(
        sim.configs().data(), sim.configs().size(), viable, joined);
    Token tok = ll_search_peek_ahead<T>(c, s, ahead, look, viable, !inHand);
    if (ll_search_spent(s)) return arm;
    ll_search_at_end<T>(c, s, ahead, tok);
    sim.at_token(tokens);
    const bool planned = k < s.plan.size() && s.plan[k].at == gap &&
                         s.plan[k].begin == tok.begin;
    // The conditions on the gap are asked as the search asked them: of the
    // token a re-reading reads, and of the one in hand under an insertion or no
    // edit. A deletion, a skip and an abandon ask nothing of it.
    if constexpr (T.ngapConds != 0) {
      const LlEdit::Kind kind = planned ? s.plan[k].kind : LlEdit::ins;
      if (kind == LlEdit::ins && !ll_search_filter<T>(c, s, tok, gap))
        return arm;
      if (kind == LlEdit::rep &&
          !ll_search_filter<T>(
              c, s, Token{tok.begin, s.plan[k].end, s.plan[k].term}, gap))
        return arm;
    }
    if (planned) {
      if (const LlEdit& g = s.plan[k++]; g.kind == LlEdit::ins) {
        if (!sim.step(g.term)) return first ? kNoRow : arm;
        if constexpr (kIndented<T>)
          if (indent_moves<T>(g.term)) {
            indent_shift_invented<T>(s.indent.arena, ahead.at,
                                     Token{gap, gap, g.term});
            ahead.p = gap;
            ahead.head = true;
            inHand = false;
          }
      } else if (g.kind == LlEdit::abandon) {
        if (!sim.leave(g.end, g.state)) return arm;
      } else {
        if (g.kind == LlEdit::rep) {
          sim.at_token(++tokens);
          if (!sim.step(g.term)) return arm;
        }
        ll_ahead_step<T>(
            s.indent.arena, ahead,
            Token{tok.begin, g.end, g.kind == LlEdit::del ? tok.sym : g.term},
            g.kind != LlEdit::rep);
        gap = g.end;
        shifts = 0;
        inHand = false;
      }
      arm = sim.lowest();
      if (sim.decided() != kNoRow) return arm;
      continue;
    }
    if (tok.sym == kInvalidSymbol ||
        (k >= s.plan.size() &&
         (shifts >= kLlArmReach || sim.decided() != kNoRow)))
      return arm;
    if (ll_ends<T>(c, tok.sym)) {
      if (sim.step(tok.sym)) arm = sim.lowest();
      return arm;
    }
    ll_search_terms<T>(c, s, sim.configs().data(), sim.configs().size(), tok,
                       s.terms);
    if (ll_search_spent(s)) return arm;
    if (!ll_search_synthetic<T>(tok)) ++tokens;
    sim.at_token(tokens);
    if (!sim.step(s.terms.data(), s.terms.size())) return arm;
    arm = sim.lowest();
    if (sim.decided() != kNoRow) return arm;
    ll_ahead_step<T>(s.indent.arena, ahead, tok, false);
    gap = tok.end;
    ++shifts;
    inHand = false;
  }
}

// What a pause is told: the edit to write at it, the arm of the root's
// choice the elected path enters, and whether the search could not answer.
struct LlPause {
  const LlEdit* edit = nullptr;
  std::uint16_t arm = kNoRow;
  bool degraded = false;
};

// The search for a fault at this pause: its elected path becomes the plan.
template <const auto& T>
void ll_search_new(LlContext& c, LlSearch& s, const LlRoot& root) {
  const std::size_t depth = c.stack.size();
  bool exhausted = false, raced = false;
  std::uint32_t elected = kLlNoNode;
  for (std::size_t frames = kLlSearchFrames;; frames *= 2) {
    ll_search_reset(s);
    s.frames = frames;
    const std::uint32_t r = ll_search_root<T>(c, s, root, frames);
    ll_search_add<T>(c, s, r);
    elected = ll_search_run<T>(c, s, exhausted, raced);
    if (frames >= depth || !s.sim.unbound()) break;
  }
  s.root = root;
  s.madeAt = c.at;
  s.madeBegin = c.la.begin;
  s.planned = elected != kLlNoNode;
  s.arm = kNoRow;
  if (s.planned) {
    ll_search_plan(s, elected);
    if (root.decision != kNoRow) {
      s.arm = ll_search_arm_of<T>(c, s, root.decision);
      const LlReading& e = s.nodes[elected];
      std::uint16_t lowest = kNoRow;
      for (std::uint32_t k = 0; k < e.ncfg; ++k)
        lowest = std::min(lowest, s.pool[e.cfg + k].alt);
      if (s.arm == kNoRow) s.arm = lowest;
    }
  }
  if (ll_search_spent(s) || exhausted || raced) ll_search_bounded<T>(c, false);
  if (s.work > s.budget && !s.degraded) {
    s.degraded = true;
    ll_search_bounded<T>(c, true);
  }
}

// The edit to write at this pause: from the plan in hand while it covers the
// pause, else from a search from `root`.
template <const auto& T>
LlPause ll_pause(LlContext& c, const LlRoot& root) {
  LlSearch& s = ll_search_of<T>(c);
  if (s.planned) {
    const LlEdit* g = s.next < s.plan.size() ? &s.plan[s.next] : nullptr;
    const bool here = g != nullptr && g->at == c.at && g->begin == c.la.begin;
    if (root == s.root && c.at == s.madeAt && c.la.begin == s.madeBegin)
      return {here ? g : nullptr, s.arm, false};
    if (here && (g->kind != LlEdit::ins || root.decision == kNoRow))
      return {g, kNoRow, false};
    const bool ahead =
        g != nullptr && !here &&
        (c.at < g->at || (c.at == g->at && c.la.begin < g->begin));
    if ((here || ahead) && root.decision != kNoRow)
      if (const std::uint16_t arm = ll_search_arm_of<T>(c, s, root.decision);
          arm != kNoRow)
        return {here ? g : nullptr, arm, false};
  }
  if (s.degraded) return {nullptr, kNoRow, true};
  ll_search_new<T>(c, s, root);
  if (!s.planned) return {nullptr, kNoRow, true};
  const LlEdit* g = s.next < s.plan.size() ? &s.plan[s.next] : nullptr;
  return {g != nullptr && g->at == c.at && g->begin == c.la.begin ? g : nullptr,
          s.arm, false};
}

}  // namespace cursiv::engine
