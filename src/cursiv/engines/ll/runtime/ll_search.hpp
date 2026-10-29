// The repair search: best-first over the gestures a refused reading can make,
// each reading read ahead by the graph's own step until three tokens hold it.
// Among the cheapest, the one read furthest on is elected, and its path is
// written one gesture per pause.
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/engines/ll/runtime/LlAtoms.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/LlGesture.hpp"
#include "cursiv/engines/ll/runtime/LlOwed.hpp"
#include "cursiv/engines/ll/runtime/LlReading.hpp"
#include "cursiv/engines/ll/runtime/LlSearch.hpp"
#include "cursiv/engines/ll/runtime/ll_indent.hpp"
#include "cursiv/engines/ll/runtime/ll_lex_reading.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/engines/ll/runtime/ll_note.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/ll/runtime/ll_rescan.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/runtime/TypoCandidates.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

inline void ll_search_clear(auto&... x) { (x.clear(), ...); }

inline std::uint32_t ll_search_link(LlSearch& s, const LlGesture& g) {
  s.links.push_back(g);
  return static_cast<std::uint32_t>(s.links.size()) - 1;
}

// What a gesture is to the expected set: the terminal it inserts, if any.
inline std::uint32_t ll_search_kind(const LlGesture& g) {
  return g.kind == LlGesture::ins ? std::uint32_t{g.term}
                                  : std::uint32_t{kNoElement};
}

inline std::strong_ordering ll_key_cmp(const LlReading& a, const LlReading& b) {
  return std::tie(a.cost, a.lost, a.faults, a.invented) <=>
         std::tie(b.cost, b.lost, b.faults, b.invented);
}

inline bool ll_search_goal(const LlReading& r) {
  // A choice may begin before the actual refusal. Its sound prefix does not
  // validate a repair: the three tokens must follow at least one gesture.
  return r.done || (r.faults != 0 && r.shifts >= kRepairValidateShifts &&
                    r.settled);
}

inline bool ll_entry_after(const LlSearch::Entry& a, const LlSearch::Entry& b) {
  return std::tie(a.f, a.lost, a.faults, a.invented, a.seq) >
         std::tie(b.f, b.lost, b.faults, b.invented, b.seq);
}

// A reading that took a token since its last gesture opens a fault with the
// next one.
inline void ll_search_queue(LlSearch& s, std::uint32_t f, const LlReading& r,
                            std::uint32_t at, std::uint32_t lost = 0) {
  const std::uint32_t opens = !r.gen && !ll_search_goal(r) && r.fresh ? 1u : 0u;
  s.heap.push_back(LlSearch::Entry{f, r.lost + lost, r.faults + opens,
                                   r.invented, s.seq++, at});
  std::push_heap(s.heap.begin(), s.heap.end(), ll_entry_after);
  s.nodes[at].queued = true;
}

// Once a gesture's readings are admitted, the tail no queue entry names is
// given back; where none is left, so is the pool they laid, and where none
// handed its link on, the links.
inline void ll_search_trim(LlSearch& s, const LlSearchLaid& m) {
  while (s.nodes.size() > m.nodes && !s.nodes.back().queued) s.nodes.pop_back();
  if (s.facts.reading >= s.nodes.size()) s.facts.cfg = kLlNoNode;
  if (s.nodes.size() != m.nodes) return;
  s.pool.resize(m.pool);
  if (s.handed == m.handed) s.links.resize(m.links);
}

// What a reading's future depends on, its owed run aside: two readings with
// the same one gain the same increments of key, the one holding the owed run
// perhaps more. Completed positions matter too: they decide whether another
// token is owed or merely optional in that call.
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
  mix(r.ahead.p);
  mix(r.gapFrom);
  mix(r.tokens);
  mix(r.ahead.head);
  mix(r.shifts | std::uint64_t{r.phase} << 8 | std::uint64_t{r.fresh} << 16 |
      std::uint64_t{r.done} << 17 | std::uint64_t{r.faults != 0} << 18 |
      std::uint64_t{r.settled} << 19 | std::uint64_t{r.inHand} << 20);
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
  if (a.ahead.p != b.ahead.p || a.gapFrom != b.gapFrom ||
      a.tokens != b.tokens || a.ahead.head != b.ahead.head ||
      a.shifts != b.shifts || a.phase != b.phase || a.fresh != b.fresh ||
      a.done != b.done || (a.faults != 0) != (b.faults != 0) ||
      a.settled != b.settled)
    return false;
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

// A reading dies to another of its future at a worse key and no longer an
// owed run, or at an equal key and run, where the kept one takes the better
// rank's path: what descends from it follows, and what the dropped path first
// inserted stays named. Two at an equal key and different runs both stand, as
// two that neither outdoes do.
template <const auto& T>
void ll_search_admit(LlSearch& s, std::uint32_t i) {
  LlReading& r = s.nodes[i];
  const LlSearch::Future future = ll_search_future<T>(s, r);
  auto [it, last] = s.seen.equal_range(future);
  while (it != last && !ll_search_same_future<T>(s, r, s.nodes[it->second]))
    ++it;
  if (it == last) {
    s.seen.emplace(future, i);
  } else {
    LlReading& x = s.nodes[it->second];
    const auto k = x.gone ? std::strong_ordering::less : ll_key_cmp(r, x);
    if (k > 0 ? r.owing <= x.owing : k == 0 && r.owing == x.owing) {
      if (k == 0) {
        const GestureRank::Parting p = s.rank.part(s.links, r.link, x.link);
        const bool handed = r.link != x.link && GestureRank::before(s.links, p);
        s.work +=
            s.tied.tie(s.links, x.link, r.link, p, handed, ll_search_kind);
        if (handed) {
          s.links[x.link] = s.links[r.link];
          ++s.handed;
        }
      }
      r.gone = true;
      return;
    }
    if (x.gone || (k < 0 && x.owing <= r.owing)) {
      x.gone = true;
      it->second = i;
    }
  }
  ll_search_queue(
      s, r.cost + (!ll_search_goal(r) && !r.owing ? kRepairReread : 0), r, i);
}

inline void ll_search_load(LlSearch& s, const LlReading& r) {
  s.sim.load(s.pool.data() + r.cfg, r.ncfg);
  s.sim.at_token(r.tokens);
}

// The simulation's set laid in the pool as the reading's own: the positions
// that can take a token or end their rule, the others being routes to those.
template <const auto& T>
void ll_search_keep(LlSearch& s, LlReading& r) {
  const LlOwed<T>& owed = ll_owed<T>();
  r.cfg = static_cast<std::uint32_t>(s.pool.size());
  if (s.facts.cfg >= r.cfg) s.facts.cfg = kLlNoNode;
  std::uint16_t first = kNoRow;
  r.settled = true;
  for (const LlConfig& x : s.sim.configs())
    if (owed.standing(x.state)) {
      s.pool.push_back(x);
      if (first == kNoRow) first = x.alt;
      r.settled = r.settled && first == x.alt;
    }
  r.ncfg = static_cast<std::uint32_t>(s.pool.size()) - r.cfg;
}

// Whether the search's share is spent, the walks since it last asked paid.
inline bool ll_search_spent(LlSearch& s) {
  const std::uint64_t paid = (s.sim.visits() - s.visited) / kLlVisitsPerWork;
  s.work += paid;
  s.visited += paid * kLlVisitsPerWork;
  return s.work > s.limit;
}

inline void ll_search_cut(LlSearch& s) {
  if (s.work <= s.limit) s.work = s.limit + 1;
}

inline std::uint64_t ll_search_bytes(const LlSearch& s) {
  const std::uint64_t left = s.limit > s.work ? s.limit - s.work : 0;
  return left > ~std::uint64_t{0} / kRepairLexBytes
             ? ~std::uint64_t{0}
             : left * kRepairLexBytes;
}

inline TextOffset ll_search_walk_end(const LlContext& c, const LlSearch& s,
                                     TextOffset p) {
  const std::uint64_t bytes = ll_search_bytes(s);
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
                    std::uint16_t mode, TextOffset p,
                    TextOffset upto = kLlUnscanned) {
  Scan& sc = row[mode];
  if (sc.end == kLlUnscanned) {
    c.stop.check();
    const TextOffset end = std::min(ll_search_walk_end(c, s, p), upto);
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
// obeys the same share as the reading that needs the boundary.
template <const auto& T>
TextOffset ll_search_text_end(const LlContext& c, LlSearch& s, TextOffset at,
                              TextOffset upto) {
  if constexpr (T.nsections != 0) {
    if (c.section != 0)
      for (TextOffset p = at; p < upto && !ll_search_spent(s);) {
        const Scan sc = ll_search_scan<T>(c, s, ll_search_row<T>(s, p),
                                          ll_err_mode<T>(c), p, upto);
        if (ll_search_spent(s)) break;
        if (sc.term == kInvalidSymbol || sc.end <= p) {
          ++p;
        } else {
          if (ll_ends<T>(c, sc.term)) return p;
          p = sc.end;
        }
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

// The visible token, trivia crossed unless `cross` is false; only complete,
// level-independent reads enter the memo. An artificial end never synthesizes
// EOF or a dedent.
template <const auto& T>
Token ll_search_peek_ahead(
    const LlContext& c, LlSearch& s, LlAhead& ahead,
    std::uint16_t look = kNoRow,
    const LlMask<T.termWords>* viable = nullptr, bool cross = true) {
  const auto mode = ll_err_mode<T>(c);
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
      const bool joined = kIndented<T> && ahead.at.depth != 0;
      const auto blanks = joined ? T.modeJoinedTrivia[look] : T.modeTrivia[look];
      const Scan blank = ll_search_scan<T>(c, s, ll_search_row<T>(s, p),
                                           blanks, p);
      if (ll_search_spent(s)) return Token{p, p, kInvalidSymbol};
      if (blank.term != kInvalidSymbol && blank.end > p) {
        const TextOffset cap = ll_search_walk_end(c, s, p);
        TextOffset reached = p;
        std::vector<std::uint16_t> visible;
        const TextOffset end =
            ll_match_in<T>(c.src, cap, p, *viable, visible, &reached);
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
    bool blank = cross && look == kNoRow && T.terms[sc.term].trivia;
    if constexpr (kIndented<T>)
      blank = blank || (cross && look == kNoRow && T.terms[sc.term].joins &&
                        ahead.at.depth != 0);
    if (!blank) return Token{p, sc.end, sc.term, mode};
    p = sc.end;
    head = false;
  }
}

// A single skip configuration belongs to the active positions, even where
// there is no choice (and thus no lookMode table). Mixed configurations keep
// the broad reading; none may lend its trivia to the others.
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
      const auto e = state.edge + j;
      const auto& edge = T.atn.edge(e);
      if (edge.kind != LlEdgeKind::atom && edge.kind != LlEdgeKind::condition)
        continue;
      if (edge.kind == LlEdgeKind::atom && edge.label != kEofSymbol &&
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

// The token in hand as the positions' own alphabet reads it, as the descent
// does: the broad one may read a terminal none of them takes over a blank
// they skip, a line's end over a statement's. The descent's own token in hand
// is read where it begins: its gap is the point's, which may be a caller's
// that skips less than the positions do.
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
      ll_search_peek_ahead<T>(c, s, r.ahead, look, &viable, !r.inHand);
  if (memo && !ll_search_spent(s)) s.lexed.emplace(r.ahead.p, t);
  return t;
}

// All readings of the token's bytes in the positions' shared alphabet. A
// keyword reserves its spelling from a name while both are wanted; where
// only the name is wanted, it reads as that name.
template <const auto& T>
void ll_search_terms(const LlContext& c, LlSearch& s, const LlConfig* cfg,
                     std::size_t n, const Token& tok,
                     std::vector<std::uint16_t>& out) {
  out.clear();
  if (tok.sym < T.nterms && T.terms[tok.sym].laid != 0) {
    out.push_back(tok.sym);
    return;
  }
  LlMask<T.termWords> viable;
  bool any = false;
  std::uint32_t last = ~0u;
  for (std::size_t k = 0; k < n && !ll_search_spent(s); ++k) {
    const std::uint32_t state = cfg[k].state;
    if (state == last) continue;
    last = state;
    for (const auto& a : ll_atoms<T>().at(state)) {
      viable.set(a.label);
      any = true;
    }
  }
  if (!any || ll_search_spent(s)) return;
  c.stop.check();
  const TextOffset cap = ll_search_walk_end(c, s, tok.begin);
  TextOffset reached = tok.begin;
  const TextOffset end =
      ll_match_in<T>(c.src, cap, tok.begin, viable, out, &reached);
  ++s.work;
  if (ll_search_walked(c, s, tok.begin, cap, reached) || end != tok.end)
    out.clear();
}

// How many calls deep a context stands, by its first return.
inline std::uint32_t ll_search_calls(LlSearch& s, std::uint32_t ctx) {
  std::uint32_t depth = 0;
  std::vector<std::uint32_t>& chain = s.chain;
  chain.clear();
  for (; ctx != kLlCtxNone && ctx != kLlCtxApart;
       ctx = s.sim.contexts().parent(ctx, 0)) {
    if (const auto it = s.depths.find(ctx); it != s.depths.end()) {
      depth = it->second;
      break;
    }
    chain.push_back(ctx);
  }
  for (std::size_t k = chain.size(); k-- > 0;) s.depths[chain[k]] = ++depth;
  return depth;
}

// What the positions of reading `i` offer, taken once for the reading asked
// last; the reference holds until another is asked. The terminals are in the
// order a repair tries them: the innermost call's first, then as the graph
// wrote them.
inline std::uint32_t ll_search_source_tokens(const LlContext& c, LlSearch& s,
                                             std::uint32_t frame,
                                             std::uint32_t most);

template <const auto& T>
const LlFacts& ll_search_facts(const LlContext& c, LlSearch& s,
                               std::uint32_t i) {
  const LlReading& r = s.nodes[i];
  LlFacts& f = s.facts;
  if (f.reading == i && f.cfg == r.cfg && f.n == r.ncfg) return f;
  f.reading = i;
  f.cfg = r.cfg;
  f.n = r.ncfg;
  ll_search_clear(f.viable, f.due, f.rest, f.modes, f.bits, f.written);
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
  // Filtering keeps ready positions only. The original call positions still
  // prove what an unfinished caller owes, including a required child.
  ll_search_load(s, r);
  ll_owed<T>().due(s.sim, f.due, [&](std::uint32_t ctx) {
    if (ctx == kLlCtxNone) return r.tokens != 0;
    for (unsigned k = 0; k < s.sim.contexts().count(ctx); ++k) {
      const std::int64_t start = s.sim.contexts().start(ctx, k);
      if (start >= 0 ? r.tokens > static_cast<std::uint64_t>(start)
                     : r.tokens != 0 ||
                           ll_search_source_tokens(
                               c, s, static_cast<std::uint32_t>(-1 - start), 0) != 0)
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

// The token that closes the section the descent stands in: owed only where the
// document ends inside it.
template <const auto& T>
SymbolId ll_search_closer(const LlContext& c) {
  return T.nsections != 0 && c.section != 0 ? T.sectionEnd[c.section - 1u]
                                            : kInvalidSymbol;
}

template <const auto& T>
bool ll_search_laid(const Token& t) {
  return t.sym < T.nterms && T.terms[t.sym].laid != 0;
}

enum class LlRead : std::uint8_t { shifted, refused, done, spent };

// One token read by reading `i`: the conditions on its gap asked, the
// positions stepped by it. Refusal keeps the original positions: deleting
// this token must evaluate its guards again against the next one.
template <const auto& T>
LlRead ll_search_step(LlContext& c, LlSearch& s, std::uint32_t i) {
  if (ll_search_spent(s)) return LlRead::spent;
  c.stop.poll();
  Token tok = ll_search_peek<T>(c, s, s.nodes[i]);
  if (ll_search_spent(s)) return LlRead::spent;
  LlReading& r = s.nodes[i];
  if (tok.sym == kInvalidSymbol) return LlRead::refused;
  s.work += 8;
  ll_search_load(s, r);
  // The end of input while a level is open is read as the drive reads it:
  // the end where the readings take it and no dedent, and the dedent first
  // where they take one. A line's dedent never stands at the end.
  if constexpr (kIndented<T>) {
    if (tok.sym == kEofSymbol &&
        ll_end_dedents<T>(s.indent.arena, r.ahead.at, false) &&
        ll_dedent_over_end<T>(s.sim.configs()))
      tok.sym = kLaid<T, kLaidDedent>;
    else if (kLaid<T, kLaidDedent> != 0 &&
             tok.sym == kLaid<T, kLaidDedent> && tok.begin == c.len &&
             ll_end_stands_over<T>(s.sim.configs()))
      tok.sym = kEofSymbol;
  }
  const bool ends = ll_ends<T>(c, tok.sym);
  bool passed = true;
  if constexpr (T.ngapConds != 0)
    passed = ll_search_filter<T>(c, s, tok, r.gapFrom);
  if (passed && !ends)
    ll_search_terms<T>(c, s, s.sim.configs().data(), s.sim.configs().size(),
                       tok, s.terms);
  if (ll_search_spent(s)) return LlRead::spent;
  s.sim.at_token(r.tokens + (!ends && !ll_search_laid<T>(tok) ? 1 : 0));
  if (passed && (ends ? s.sim.step(tok.sym)
                      : s.sim.step(s.terms.data(), s.terms.size()))) {
    ll_search_keep<T>(s, r);
    if (ends) {
      r.done = true;
      return LlRead::done;
    }
    r.gapFrom = tok.end;
    r.inHand = false;
    ll_ahead_step<T>(s.indent.arena, r.ahead, tok, false);
    if (!ll_search_laid<T>(tok)) {
      ++r.tokens;
      r.shifts = static_cast<std::uint8_t>(
          std::min<std::uint32_t>(r.shifts + 1, kLlArmReach));
      r.fresh = true;
      r.phase = 0;
      r.owing = false;
    }
    return LlRead::shifted;
  }
  return LlRead::refused;
}

// Reading `i` reads until it is refused, holds or ends: that reading is
// `s.out`, where the work its share pays for allows.
template <const auto& T>
void ll_search_read(LlContext& c, LlSearch& s, std::uint32_t i) {
  s.out.clear();
  for (;;) {
    if (s.nodes[i].gone) return;
    if (ll_search_goal(s.nodes[i])) break;
    const LlRead read = ll_search_step<T>(c, s, i);
    if (read == LlRead::spent) return;
    if (read != LlRead::shifted) break;
  }
  s.out.push_back(i);
}

template <const auto& T>
void ll_search_lay(LlContext& c, LlSearch& s, std::uint32_t e) {
  ll_search_read<T>(c, s, e);
  for (const std::uint32_t q : s.out) ll_search_admit<T>(s, q);
}

inline std::uint32_t ll_search_child(LlSearch& s, std::uint32_t from,
                                     LlGesture::Kind kind, SymbolId term,
                                     TextOffset end, std::uint32_t choice,
                                     TextOffset begin) {
  s.nodes.push_back(s.nodes[from]);
  LlReading& e = s.nodes.back();
  e.link = ll_search_link(s, LlGesture{.parent = e.link,
                                       .choice = choice,
                                       .kind = kind,
                                       .term = term,
                                       .end = end,
                                       .at = e.gapFrom,
                                       .begin = begin});
  e.faults += e.fresh ? 1u : 0u;
  e.fresh = false;
  // An insertion or an abandon leaves the token in hand where it stood.
  e.inHand = e.inHand && (kind == LlGesture::ins || kind == LlGesture::abandon);
  e.shifts = 0;
  e.gone = e.queued = e.gen = e.genAbandon = false;
  ++s.work;
  return static_cast<std::uint32_t>(s.nodes.size()) - 1;
}

// A terminal the readings may invent in front of the token in hand: no implied
// one, nor the one that closes a section but at its end.
template <const auto& T>
bool ll_search_insertable(const LlContext& c, std::uint16_t term, bool atEnd) {
  return term != kEofSymbol && T.terms[term].laid != kLaidImplied &&
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
// it carry on, and an owed one that follows an owed one costs nothing. Its
// readings are admitted, and what they leave is given back.
template <const auto& T>
void ll_search_insert(LlContext& c, LlSearch& s, std::uint32_t bi,
                      std::uint16_t term, std::uint32_t choice, bool owed,
                      TextOffset begin) {
  const LlSearchLaid pools(s);
  const LlReading b = s.nodes[bi];
  ll_search_load(s, b);
  if constexpr (T.ngapConds != 0) {
    const Token tok = ll_search_peek<T>(c, s, s.nodes[bi]);
    if (ll_search_spent(s) || !ll_search_filter<T>(c, s, tok, b.gapFrom))
      return;
  }
  if (!s.sim.step(term)) return;
  const std::uint32_t e =
      ll_search_child(s, bi, LlGesture::ins, term, 0, choice, begin);
  LlReading& r = s.nodes[e];
  ll_search_keep<T>(s, r);
  r.cost += owed && b.owing ? 0 : kRepairGesture;
  r.owing = owed;
  r.invented += 1;
  r.phase = 2;
  // A level the token moves is read again, as the descent reads it.
  if constexpr (kIndented<T>)
    if (indent_moves<T>(term)) {
      indent_shift_invented<T>(s.indent.arena, r.ahead.at,
                               Token{b.gapFrom, b.gapFrom, term});
      r.inHand = false;
    }
  ll_search_lay<T>(c, s, e);
  ll_search_trim(s, pools);
}

// The insertions that are not owed, laid as the entry that stood for them is
// asked for.
template <const auto& T>
void ll_search_inserts(LlContext& c, LlSearch& s, std::uint32_t gi) {
  const std::uint32_t from = s.nodes[gi].genFrom;
  const std::uint32_t rank = s.nodes[gi].genKeep;
  const Token tok = ll_search_peek<T>(c, s, s.nodes[from]);
  if (ll_search_spent(s)) return;
  const bool atEnd = ll_ends<T>(c, tok.sym);
  ll_search_each_insert<T>(
      c, s, from, [&](std::uint16_t t, unsigned k, bool owed) {
        if (ll_search_spent(s)) return false;
        if (owed || !ll_search_insertable<T>(c, t, atEnd)) return true;
        ll_search_insert<T>(c, s, from, t, rank + k, false, tok.begin);
        return true;
      });
}

template <const auto& T>
const LexReading& ll_search_reading(LlContext& c, LlSearch& s, TextOffset p) {
  static constexpr LexReading kNone{};
  const auto mode = ll_err_mode<T>(c);
  if (p >= c.len ||
      !T.lexOpeners[mode].has(static_cast<unsigned char>(c.src[p])))
    return kNone;
  if (const auto it = s.readings.find(p); it != s.readings.end())
    return it->second;
  if (c.reads.spent()) return kNone;
  const TextOffset textEnd =
      ll_search_text_end<T>(c, s, p, ll_search_walk_end(c, s, p));
  if (ll_search_spent(s)) return kNone;
  const TextOffset end = std::min(textEnd, ll_search_walk_end(c, s, p));
  TextOffset reached = p;
  c.stop.check();
  const LexReading r = ll_lex_reading<T>(c.src, end, mode, p, reached);
  c.reads.pay(p, reached);
  ++s.work;
  if (ll_search_walked(c, s, p, end, reached, textEnd)) return kNone;
  return s.readings.emplace(p, r).first->second;
}

// The token in hand read as `term` up to `end`: `rep` where its bytes are
// re-read as a terminal the positions step by, `skip` where they are a trivia
// the lexer refused. Its readings go to `s.out`.
template <const auto& T>
void ll_search_reread(LlContext& c, LlSearch& s, std::uint32_t bi,
                      LlGesture::Kind kind, SymbolId term, TextOffset end,
                      std::uint32_t choice, const Token& tok) {
  s.out.clear();
  const std::uint32_t e =
      ll_search_child(s, bi, kind, term, end, choice, tok.begin);
  LlReading& r = s.nodes[e];
  r.cost += kRepairReread;
  r.owing = false;
  const Token read{tok.begin, end, term};
  if (kind == LlGesture::skip) {
    r.phase = 3;
    r.gapFrom = end;
    ll_ahead_step<T>(s.indent.arena, r.ahead, read, true);
  } else {
    ll_search_load(s, r);
    if constexpr (T.ngapConds != 0)
      if (!ll_search_filter<T>(c, s, read, r.gapFrom)) return;
    s.sim.at_token(r.tokens + 1);
    if (!s.sim.step(term)) return;
    ++r.tokens;
    ll_search_keep<T>(s, r);
    r.gapFrom = end;
    ll_ahead_step<T>(s.indent.arena, r.ahead, read, false);
    r.fresh = true;
    r.phase = 0;
  }
  ll_search_read<T>(c, s, e);
}

// The token in hand as the lexer's refused reading of it. Closed, one
// re-reading; stopped by its own line's end, every end that cuts no word, the
// shortest first; open further, its line ends in order up to the first a goal
// holds after, then the end of the text. A trivia so read is skipped, the
// author's bytes kept. The ends are found once per search, and only as far as
// the share lets the readings they open be read.
template <const auto& T>
void ll_search_lexical(LlContext& c, LlSearch& s, std::uint32_t bi,
                       const Token& tok, std::uint32_t& choice) {
  const LexReading r = ll_search_reading<T>(c, s, tok.begin);
  if (r.term == kInvalidSymbol || r.term == ll_search_closer<T>(c)) return;
  const bool trivia = T.terms[r.term].trivia;
  if (!trivia) {
    const std::vector<std::uint16_t>& viable = ll_search_facts<T>(c, s, bi).viable;
    if (s.nodes[bi].phase > 2 ||
        std::ranges::find(viable, r.term) == viable.end())
      return;
  }
  const auto one = [&](TextOffset end) {
    const LlSearchLaid pools(s);
    ll_search_reread<T>(c, s, bi, trivia ? LlGesture::skip : LlGesture::rep,
                        r.term, end, choice++, tok);
    bool holds = false;
    for (const std::uint32_t q : s.out) {
      holds = holds || ll_search_goal(s.nodes[q]);
      ll_search_admit<T>(s, q);
    }
    ll_search_trim(s, pools);
    return holds;
  };
  if (r.closed) {
    one(r.end);
    return;
  }
  RepairEnds& ends = s.ends[tok.begin];
  const auto cuts = [&](TextOffset x) { return lex_cuts<T>(c.src, c.len, x); };
  for (std::size_t k = 0; !ll_search_spent(s); ++k) {
    if (k == ends.found.size()) {
      const TextOffset from = ends.regime == RepairEnds::none
                                  ? tok.begin : ends.scanned;
      bool cut = false;
      if (!ends.more(c.src, c.len, tok.begin, r, s.work, cuts,
                     ll_search_walk_end(c, s, std::min(from, c.len)), &cut,
                     &c.stop, s.limit)) {
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
  const TextOffset len = ll_search_text_end<T>(
      c, s, tok.begin, ll_search_walk_end(c, s, tok.begin));
  const std::uint64_t most = ll_search_bytes(s);
  const auto reach = static_cast<TextOffset>(
      std::min<std::uint64_t>(most, kLlUnscanned));
  const TextOffset horizon = typo_horizon(tok.begin, tok.end, len, reach);
  std::uint64_t read = 0;
  bool cut = ll_search_spent(s);
  for (std::size_t m = 0; m < f.modes.size() && !cut; ++m)
    ll_rescan_within<T>(c, f.modes[m], f.bits.data() + m * T.termWords,
                        tok.begin, tok.end, len, horizon, read, found, most,
                        cut);
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

template <const auto& T>
TextOffset ll_search_drop_end(LlContext& c, LlSearch& s, const Token& tok) {
  if (tok.end != tok.begin) return tok.end;
  if (const auto it = s.drops.find(tok.begin); it != s.drops.end())
    return it->second;
  const LexReading r = ll_search_reading<T>(c, s, tok.begin);
  if (ll_search_spent(s)) return tok.begin;
  if (r.term != kInvalidSymbol)
    return s.drops.emplace(tok.begin, lex_cut(tok.begin, r)).first->second;
  const TextOffset cap = ll_search_walk_end(c, s, tok.begin);
  TextOffset reached = tok.begin;
  const TextOffset end = ll_unmatched_end<T>(c, tok.begin, cap, reached);
  ++s.work;
  if (!ll_search_walked(c, s, tok.begin, cap, reached))
    s.drops.emplace(tok.begin, end);
  return end;
}

// Whether refused reading `bi` may go on by a re-reading of the token in hand.
template <const auto& T>
bool ll_search_rereads(LlContext& c, LlSearch& s, std::uint32_t bi,
                       const Token& tok) {
  if (ll_ends<T>(c, tok.sym) || ll_search_laid<T>(tok)) return false;
  if (ll_search_reading<T>(c, s, tok.begin).term != kInvalidSymbol) return true;
  return (s.nodes[bi].phase <= 2 &&
          !ll_search_typos<T>(c, s, bi, tok).empty()) ||
         ll_search_spent(s);
}

// The author's real tokens read by actual call `frame` before this pause.
// Calls are nested, so one backward walk answers every frame it passes.
inline std::uint32_t ll_search_source_tokens(const LlContext& c, LlSearch& s,
                                             std::uint32_t frame,
                                             std::uint32_t most) {
  const CstRun& run = c.tape.stream();
  for (std::size_t i = c.stack.size(); i-- > frame;) {
    if (s.sourceTokens[i] != kLlNoNode) continue;
    while (s.sourceScanned > c.stack.begin(i)) {
      if (ll_search_spent(s)) return 0;
      if (s.sourceCount > most) return s.sourceCount;
      const CstNode& n = run.at(--s.sourceScanned);
      s.sourceCount += !n.hidden && !n.recovered && n.subtreeSize == 1 &&
                               n.end > n.begin
                           ? 1u
                           : 0u;
      ++s.work;
    }
    s.sourceTokens[i] = s.sourceCount;
  }
  return s.sourceTokens[frame];
}

// Count and lay an abandon only when the queue reaches what it costs. A
// large unfinished call must not spend a cheap repair's share counting its
// entire prefix first.
template <const auto& T>
void ll_search_abandon(LlContext& c, LlSearch& s, std::uint32_t gi,
                        std::uint32_t f) {
  const LlReading g = s.nodes[gi];
  const LlReading b = s.nodes[g.genFrom];
  const auto frame = static_cast<std::uint32_t>(-1 - g.genStart);
  const std::uint32_t most = (f - b.cost) / kRepairGesture;
  const std::uint32_t lost =
      g.genStart >= 0
          ? b.tokens - static_cast<std::uint32_t>(g.genStart)
          : b.tokens + ll_search_source_tokens(
                           c, s, frame, most > b.tokens ? most - b.tokens : 0);
  if (lost == 0 || ll_search_spent(s)) return;
  const std::uint32_t cost = b.cost + lost * kRepairGesture;
  if (cost > f) {
    LlReading& r = s.nodes[gi];
    r.cost = cost;
    r.lost = b.lost + lost;
    ll_search_queue(s, cost, r, gi);
    return;
  }
  const LlSearchLaid pools(s);
  s.sim.at_token(b.tokens);
  s.sim.seed(g.genRet, g.genParent);
  std::vector<LlConfig> resumed(s.sim.configs());
  for (LlConfig& x : resumed) x.alt = g.genAlt;
  s.sim.load(resumed);
  const Token tok = ll_search_peek<T>(c, s, s.nodes[g.genFrom]);
  if (ll_search_spent(s)) return;
  const std::uint32_t e = ll_search_child(
      s, g.genFrom, LlGesture::abandon, g.genOwed, g.genKeep,
      g.genChoice, tok.begin);
  s.links[s.nodes[e].link].state = g.genRet;
  LlReading& r = s.nodes[e];
  ll_search_keep<T>(s, r);
  r.cost = cost;
  r.lost += lost;
  r.phase = 1;
  r.owing = false;
  const LlRead read = ll_search_step<T>(c, s, e);
  if (read == LlRead::shifted || read == LlRead::done)
    ll_search_lay<T>(c, s, e);
  ll_search_trim(s, pools);
}

// Leave a partially read call, retaining its exact continuation. Context
// forks may share callers; visit each (context, depth, arm) only once.
template <const auto& T>
void ll_search_abandons(const LlContext& c, LlSearch& s, std::uint32_t bi,
                         std::uint32_t& choice) {
  const LlReading b = s.nodes[bi];
  if (b.phase != 0) return;
  const std::vector<std::uint16_t> due = ll_search_facts<T>(c, s, bi).due;
  ll_search_load(s, b);
  SymbolId owed = ll_owed<T>().first(s.sim);
  if (owed == kNoElement && due.size() == 1) owed = due.front();
  struct At {
    std::uint32_t ctx, depth;
    std::uint16_t alt;
    bool operator==(const At&) const = default;
  };
  struct AtHash {
    std::size_t operator()(const At& a) const {
      return std::hash<std::uint64_t>{}(std::uint64_t{a.ctx} << 32 ^
                                        std::uint64_t{a.depth} << 16 ^ a.alt);
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
      g.gen = g.genAbandon = true;
      g.genFrom = bi;
      g.genKeep = at.depth;
      g.genRet = ret;
      g.genParent = parent;
      g.genStart = start;
      g.genAlt = at.alt;
      g.genOwed = owed;
      g.genChoice = choice++;
      g.cost += lost * kRepairGesture;
      g.lost += lost;
      g.faults += b.fresh ? 1u : 0u;
      s.nodes.push_back(g);
      ll_search_queue(s, g.cost, g,
                      static_cast<std::uint32_t>(s.nodes.size()) - 1);
    }
  }
}

// The gestures the canonical form admits, in rank order: re-readings, owed
// insertions, the others, the drop.
template <const auto& T>
void ll_search_expand(LlContext& c, LlSearch& s, std::uint32_t bi) {
  const LlReading b = s.nodes[bi];
  const Token tok = ll_search_peek<T>(c, s, s.nodes[bi]);
  if (ll_search_spent(s)) return;
  const bool atEnd = ll_ends<T>(c, tok.sym);
  const bool laid = ll_search_laid<T>(tok);
  std::uint32_t choice = 0;
  if (!atEnd && !laid) ll_search_lexical<T>(c, s, bi, tok, choice);
  if (b.phase <= 2 && !atEnd && !laid) {
    for (const Typo& t : ll_search_typos<T>(c, s, bi, tok)) {
      if (ll_search_spent(s)) break;
      if (t.term == ll_search_closer<T>(c)) continue;
      const LlSearchLaid pools(s);
      ll_search_reread<T>(c, s, bi, LlGesture::rep, t.term, t.end, choice++,
                          tok);
      for (const std::uint32_t q : s.out) ll_search_admit<T>(s, q);
      ll_search_trim(s, pools);
    }
  }
  // Bytes no alphabet reads refuse every reading alike: insertions in front of
  // them are told apart only past them, so one is made there, never a run.
  const bool garbage = !atEnd && !laid && tok.sym == kInvalidSymbol;
  if (b.phase < 2 || (b.phase == 2 && !garbage)) {
    // Only an owed run costs nothing past its first, so the terminals that are
    // not owed wait behind one entry until a reading that dear is asked for.
    const std::uint32_t insertRank = choice;
    bool deferred = false;
    ll_search_each_insert<T>(
        c, s, bi, [&](std::uint16_t t, unsigned k, bool owed) {
          if (!ll_search_insertable<T>(c, t, atEnd)) return true;
          if (ll_search_spent(s)) return false;
          if (!owed) {
            deferred = true;
            return true;
          }
          ll_search_insert<T>(c, s, bi, t, insertRank + k, true, tok.begin);
          return true;
        });
    // Ranks need order, not consecutive values: two terminal-sized ranges put
    // owed insertions first, then all others in declaration order.
    choice += 2u * T.nterms;
    if (deferred) {
      LlReading g = b;
      g.gen = true;
      g.genFrom = bi;
      g.genKeep = insertRank + T.nterms;
      g.cost += kRepairGesture;
      g.faults += b.fresh ? 1u : 0u;
      g.invented += 1;
      s.nodes.push_back(g);
      ll_search_queue(s, g.cost, g,
                      static_cast<std::uint32_t>(s.nodes.size()) - 1);
    }
  }
  if (!atEnd) {
    const TextOffset end = laid ? tok.end : ll_search_drop_end<T>(c, s, tok);
    const LlSearchLaid pools(s);
    const std::uint32_t e =
        ll_search_child(s, bi, LlGesture::del, 0, end, choice++, tok.begin);
    LlReading& r = s.nodes[e];
    r.cost += kRepairGesture;
    r.lost += laid ? 0u : 1u;
    r.phase = 3;
    r.owing = false;
    r.gapFrom = end;
    ll_ahead_step<T>(s.indent.arena, r.ahead, Token{tok.begin, end, tok.sym},
                     true);
    ll_search_lay<T>(c, s, e);
    ll_search_trim(s, pools);
  }
  ll_search_abandons<T>(c, s, bi, choice);
}

inline constexpr TextOffset kLlReachAll = 0xFFFFFFFFu;

// The goals read on with no gesture: `reach[k]` is where candidate `k`'s
// furthest reading was refused, or `kLlReachAll`; `of[g]` is the candidate goal
// `g` stands for. The reading furthest back steps, so two bound for one future
// meet and go on as one; settled once every reading still going stands for the
// same candidates past every other's refusal. True where the search's share ran
// out first.
template <const auto& T>
bool ll_search_race(LlContext& c, LlSearch& s,
                    const std::vector<std::uint32_t>& of,
                    std::vector<TextOffset>& reach) {
  struct Runner {
    std::uint32_t node;
    std::vector<std::uint32_t> of;
  };
  std::vector<Runner> runners;
  std::vector<std::uint32_t> queue;
  const auto behind = [&](std::uint32_t a, std::uint32_t b) {
    return s.nodes[runners[a].node].ahead.p > s.nodes[runners[b].node].ahead.p;
  };
  std::ranges::fill(reach, 0);
  for (std::uint32_t g = 0; g < s.goals.size(); ++g) {
    if (s.nodes[s.goals[g]].done) {
      reach[of[g]] = kLlReachAll;
      continue;
    }
    const LlReading r = s.nodes[s.goals[g]];
    s.nodes.push_back(r);
    runners.push_back(
        Runner{static_cast<std::uint32_t>(s.nodes.size()) - 1, {of[g]}});
    queue.push_back(static_cast<std::uint32_t>(runners.size()) - 1);
    std::ranges::push_heap(queue, behind);
  }
  const auto settle = [&](const std::vector<std::uint32_t>& ks) {
    for (const std::uint32_t k : ks) reach[k] = kLlReachAll;
  };
  const auto level = [&] {
    const std::vector<std::uint32_t>& first = runners[queue[0]].of;
    TextOffset least = kLlReachAll;
    for (const std::uint32_t q : queue) {
      if (runners[q].of != first) return false;
      least = std::min(least, s.nodes[runners[q].node].ahead.p);
    }
    for (std::uint32_t k = 0; k < reach.size(); ++k)
      if (!std::ranges::binary_search(first, k) && reach[k] >= least)
        return false;
    return true;
  };
  std::unordered_multimap<LlSearch::Future, std::uint32_t, LlSearch::FutureHash>
      met;
  while (!queue.empty()) {
    c.stop.poll();
    const bool settled = level();
    if (settled || ll_search_spent(s)) {
      for (const std::uint32_t q : queue) settle(runners[q].of);
      return !settled;
    }
    std::ranges::pop_heap(queue, behind);
    const std::uint32_t w = queue.back();
    queue.pop_back();
    const std::uint32_t i = runners[w].node;
    for (auto [it, last] = met.equal_range(ll_search_future<T>(s, s.nodes[i]));
         it != last; ++it)
      if (it->second == w) {
        met.erase(it);
        break;
      }
    s.nodes[i].shifts = 0;
    const LlRead read = ll_search_step<T>(c, s, i);
    if (read == LlRead::spent) {
      settle(runners[w].of);
      for (const std::uint32_t q : queue) settle(runners[q].of);
      return true;
    }
    if (read == LlRead::done) {
      settle(runners[w].of);
      continue;
    }
    if (read != LlRead::shifted) {
      const TextOffset at = ll_search_peek<T>(c, s, s.nodes[i]).begin;
      for (const std::uint32_t k : runners[w].of)
        reach[k] = std::max(reach[k], at);
      continue;
    }
    const LlSearch::Future future = ll_search_future<T>(s, s.nodes[i]);
    auto [it, last] = met.equal_range(future);
    while (it != last &&
           !ll_search_same_future<T>(s, s.nodes[i],
                                     s.nodes[runners[it->second].node]))
      ++it;
    if (it != last) {
      std::vector<std::uint32_t> both;
      std::ranges::set_union(runners[it->second].of, runners[w].of,
                             std::back_inserter(both));
      runners[it->second].of = std::move(both);
      continue;
    }
    met.emplace(future, w);
    queue.push_back(w);
    std::ranges::push_heap(queue, behind);
  }
  return false;
}

// Two paths that make the same gestures are one candidate.
inline std::uint64_t ll_gestures_fold(const std::vector<LlGesture>& links,
                                      std::uint32_t l) {
  std::uint64_t h = 0;
  for (; links[l].parent != kNoGestureLink; l = links[l].parent) {
    const LlGesture& g = links[l];
    h = (h ^ (std::uint64_t{g.kind} << 56) ^ (std::uint64_t{g.term} << 40) ^
         (std::uint64_t{g.end} << 20) ^ g.state ^ g.at ^
         (std::uint64_t{g.begin} << 8)) *
        0x100000001b3ull;
  }
  return h;
}

inline bool ll_same_gestures(const std::vector<LlGesture>& links,
                             std::uint32_t a, std::uint32_t b) {
  for (; a != b; a = links[a].parent, b = links[b].parent) {
    if (links[a].parent == kNoGestureLink || links[b].parent == kNoGestureLink)
      return links[a].parent == links[b].parent;
    const LlGesture &x = links[a], &y = links[b];
    if (x.kind != y.kind || x.term != y.term || x.end != y.end ||
        x.state != y.state || x.at != y.at || x.begin != y.begin)
      return false;
  }
  return true;
}

// The furthest reach, then the key, then the rank. Goals that make the same
// gestures race as one candidate.
template <const auto& T>
std::uint32_t ll_search_elect(LlContext& c, LlSearch& s, bool& raced) {
  std::erase_if(s.goals, [&](std::uint32_t g) { return s.nodes[g].gone; });
  const auto goal = [&](std::size_t k) -> const LlReading& {
    return s.nodes[s.goals[k]];
  };
  const auto hash = [&](std::uint32_t l) {
    return static_cast<std::size_t>(ll_gestures_fold(s.links, l));
  };
  const auto same = [&](std::uint32_t a, std::uint32_t b) {
    return ll_same_gestures(s.links, a, b);
  };
  std::unordered_map<std::uint32_t, std::uint32_t, decltype(hash),
                     decltype(same)>
      groups(0, hash, same);
  std::vector<std::uint32_t> of(s.goals.size());
  for (std::size_t k = 0; k < s.goals.size(); ++k)
    of[k] = groups
                .try_emplace(goal(k).link,
                             static_cast<std::uint32_t>(groups.size()))
                .first->second;
  std::vector<TextOffset> reach(groups.size(), kLlReachAll);
  if (groups.size() > 1) raced = ll_search_race<T>(c, s, of, reach);
  const auto before = [&](std::size_t a, std::size_t b) {
    if (reach[of[a]] != reach[of[b]]) return reach[of[a]] > reach[of[b]];
    const auto k = ll_key_cmp(goal(a), goal(b));
    return k != 0 ? k < 0 : s.rank.before(s.links, goal(a).link, goal(b).link);
  };
  std::size_t w = 0;
  for (std::size_t k = 1; k < s.goals.size(); ++k)
    if (before(k, w)) w = k;
  s.also.clear();
  s.tied.open();
  for (std::size_t k = 0; k < s.goals.size(); ++k) {
    if (reach[of[k]] != reach[of[w]] || ll_key_cmp(goal(k), goal(w)) != 0)
      continue;
    s.tied.name(s.links, goal(k).link, ll_search_kind, s.also);
  }
  TiedInserts::order(s.also);
  return s.goals[w];
}

// A plan names the pool of the search that laid it: a search that lays another
// drops it.
inline void ll_search_reset(LlSearch& s) {
  ll_search_clear(s.pool, s.nodes, s.links, s.heap, s.seen, s.goals, s.lexed,
                  s.scans, s.readings, s.ends, s.typos, s.drops, s.plan, s.also,
                  s.tied, s.depths, s.sourceTokens);
  s.facts.cfg = kLlNoNode;
  s.sourceScanned = s.sourceCount = s.seq = 0;
  s.next = 0;
  s.planned = false;
  s.limit = s.work < s.budget
                ? s.work + std::min(s.perSearch, s.budget - s.work)
                : s.budget;
}

// With no goal before the work ran out, the reading that got furthest.
template <const auto& T>
std::uint32_t ll_search_run(LlContext& c, LlSearch& s, bool& exhausted,
                            bool& raced) {
  std::uint32_t furthest = kLlNoNode;
  exhausted = raced = false;
  while (!s.heap.empty()) {
    c.stop.poll();
    std::pop_heap(s.heap.begin(), s.heap.end(), ll_entry_after);
    const LlSearch::Entry e = s.heap.back();
    s.heap.pop_back();
    if (s.nodes[e.at].gone) continue;
    if (!s.goals.empty() && e.f > s.nodes[s.goals[0]].cost) break;
    if (ll_search_spent(s)) {
      exhausted = true;
      break;
    }
    if (s.nodes[e.at].gen) {
      if (s.nodes[e.at].genAbandon)
        ll_search_abandon<T>(c, s, e.at, e.f);
      else
        ll_search_inserts<T>(c, s, e.at);
      continue;
    }
    if (ll_search_goal(s.nodes[e.at])) {
      s.goals.push_back(e.at);
      continue;
    }
    if (furthest == kLlNoNode ||
        s.nodes[e.at].gapFrom > s.nodes[furthest].gapFrom)
      furthest = e.at;
    // A refused reading owes at least one re-reading, or a whole gesture
    // where none can take its token. Future typos can change both a token and
    // its successors, so they cannot raise this bound. Owed runs stay free.
    if (!s.nodes[e.at].owing && e.f < s.nodes[e.at].cost + kRepairGesture &&
        !ll_search_rereads<T>(c, s, e.at,
                              ll_search_peek<T>(c, s, s.nodes[e.at]))) {
      ll_search_queue(s, s.nodes[e.at].cost + kRepairGesture, s.nodes[e.at],
                      e.at, e.lost - s.nodes[e.at].lost);
      continue;
    }
    ll_search_expand<T>(c, s, e.at);
  }
  // A walk the share cut leaves the goals of its cost unread: the share
  // decided, even where the bucket emptied.
  exhausted = exhausted || ll_search_spent(s);
  return s.goals.empty() ? furthest : ll_search_elect<T>(c, s, raced);
}

inline void ll_search_plan(LlSearch& s, std::uint32_t ci) {
  std::vector<std::uint32_t> path;
  for (std::uint32_t l = s.nodes[ci].link;
       l != kNoGestureLink && s.links[l].parent != kNoGestureLink;
       l = s.links[l].parent)
    path.push_back(l);
  s.plan.clear();
  s.next = 0;
  for (std::size_t k = path.size(); k-- > 0;)
    s.plan.push_back(s.links[path[k]]);
  if (!s.plan.empty() && s.plan[0].kind == LlGesture::ins)
    std::erase(s.also, static_cast<ElementId>(s.plan[0].term));
  else
    s.also.clear();
}

// How many frames of the descent's stack a search stands on at first: deeper
// ones are read only while a reading returns out of the ones it knows.
inline constexpr std::size_t kLlSearchFrames = 256;

// The simulation opened over the context of the descent: the top `frames`
// frames of its stack, below them an unknown caller.
inline std::uint32_t ll_search_open(const LlContext& c, LlSimulation& sim,
                                    std::size_t frames) {
  sim.resume();
  sim.share_epsilon_contexts();
  sim.track_calls();
  sim.at_token(0);
  sim.keep_steps();
  sim.truncated(frames < c.stack.size());
  std::uint32_t ctx = kLlCtxNone;
  for (std::size_t i = c.stack.size() > frames ? c.stack.size() - frames : 0;
       i < c.stack.size(); ++i)
    ctx = sim.contexts().push(ctx, c.stack[i],
                              -1 - static_cast<std::int64_t>(i));
  return ctx;
}

// The root reading: the positions the descent stands in, and the text from
// the token in hand.
template <const auto& T>
std::uint32_t ll_search_root(LlContext& c, LlSearch& s, const LlRoot& root,
                             std::size_t frames) {
  LlSimulation& sim = s.sim;
  if (s.sourceTokens.empty()) {
    s.sourceTokens.assign(c.stack.size(), kLlNoNode);
    s.sourceScanned = c.tape.stream().records();
  }
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
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  }
  sim.load(roots);
  LlReading r;
  ll_search_keep<T>(s, r);
  r.ahead = ll_search_ahead<T>(c, s);
  r.inHand = true;
  r.gapFrom = c.at;
  r.link = ll_search_link(s, LlGesture{});
  s.nodes.push_back(std::move(r));
  return static_cast<std::uint32_t>(s.nodes.size()) - 1;
}

template <const auto& T>
LlSearch& ll_search_of(LlContext& c) {
  if (!c.search) c.search = std::make_unique<LlSearch>(T.atn, c.len, c.budget);
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

// The arm of the choice `decision` the plan's path enters from here: the path
// laid again over the positions of that choice and the text after it, until one
// arm stands -- whatever gestures the plan has left, they cannot name another
// -- the arm still reading where the text refuses or it stops being asked,
// the lowest if several are. The plan's next gesture is the one at this pause.
template <const auto& T>
std::uint16_t ll_search_arm_of(LlContext& c, LlSearch& s,
                               std::uint16_t decision) {
  LlSimulation& sim = s.sim;
  sim.start(T.decisions[decision].state,
            ll_search_open(c, sim, kLlSearchFrames));
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
    const Token tok =
        ll_search_peek_ahead<T>(c, s, ahead, look, &viable, !inHand);
    if (ll_search_spent(s)) return arm;
    sim.at_token(tokens);
    if constexpr (T.ngapConds != 0)
      if (!ll_search_filter<T>(c, s, tok, gap)) return arm;
    if (k < s.plan.size() && s.plan[k].at == gap &&
        s.plan[k].begin == tok.begin) {
      const LlGesture& g = s.plan[k++];
      if (g.kind == LlGesture::ins) {
        if (!sim.step(g.term)) return first ? kNoRow : arm;
        if constexpr (kIndented<T>)
          if (indent_moves<T>(g.term)) {
            indent_shift_invented<T>(s.indent.arena, ahead.at,
                                     Token{gap, gap, g.term});
            inHand = false;
          }
      } else if (g.kind == LlGesture::abandon) {
        if (!sim.leave(g.end, g.state)) return arm;
      } else {
        if (g.kind == LlGesture::rep) {
          sim.at_token(++tokens);
          if (!sim.step(g.term)) return arm;
        }
        ll_ahead_step<T>(s.indent.arena, ahead,
                         Token{tok.begin, g.end,
                               g.kind == LlGesture::del ? tok.sym : g.term},
                         g.kind != LlGesture::rep);
        gap = g.end;
        shifts = 0;
        inHand = false;
      }
      arm = sim.lowest();
      if (sim.settled() != kNoRow) return arm;
      continue;
    }
    if (tok.sym == kInvalidSymbol ||
        (k >= s.plan.size() &&
         (shifts >= kLlArmReach || sim.settled() != kNoRow)))
      return arm;
    if (ll_ends<T>(c, tok.sym)) {
      if (sim.step(tok.sym)) arm = sim.lowest();
      return arm;
    }
    ll_search_terms<T>(c, s, sim.configs().data(), sim.configs().size(), tok,
                       s.terms);
    if (ll_search_spent(s)) return arm;
    if (!ll_search_laid<T>(tok)) ++tokens;
    sim.at_token(tokens);
    if (!sim.step(s.terms.data(), s.terms.size())) return arm;
    arm = sim.lowest();
    if (sim.settled() != kNoRow) return arm;
    ll_ahead_step<T>(s.indent.arena, ahead, tok, false);
    gap = tok.end;
    ++shifts;
    inHand = false;
  }
}

// What a pause is told: the gesture to write now, the arm of the root's choice
// the elected path enters, and whether the search could not answer.
struct LlPause {
  const LlGesture* gesture = nullptr;
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
    const std::uint32_t r = ll_search_root<T>(c, s, root, frames);
    ll_search_lay<T>(c, s, r);
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

// The gesture to write at the pause the descent stands at, asked of the plan in
// hand while it covers the pause -- its next gesture is here, or ahead of a
// choice, which then takes the arm the path enters -- else of a search from
// `root`.
template <const auto& T>
LlPause ll_pause(LlContext& c, const LlRoot& root) {
  LlSearch& s = ll_search_of<T>(c);
  if (s.planned) {
    const LlGesture* g = s.next < s.plan.size() ? &s.plan[s.next] : nullptr;
    const bool here = g != nullptr && g->at == c.at && g->begin == c.la.begin;
    if (root == s.root && c.at == s.madeAt && c.la.begin == s.madeBegin)
      return {here ? g : nullptr, s.arm, false};
    if (here && (g->kind != LlGesture::ins || root.decision == kNoRow))
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
  const LlGesture* g = s.next < s.plan.size() ? &s.plan[s.next] : nullptr;
  return {g != nullptr && g->at == c.at && g->begin == c.la.begin ? g : nullptr,
          s.arm, false};
}

}  // namespace cursiv
