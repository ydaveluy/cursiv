// Reading, and the two writes a read makes on the tree: trivia goes on the tape
// in front of the token it precedes, and the token's own leaf carries the row
// its position states.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/utils/macros.hpp"
#include "cursiv/ll/runtime/LlAhead.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/ll_callers.hpp"
#include "cursiv/ll/runtime/ll_indent.hpp"
#include "cursiv/ll/runtime/ll_matcher.hpp"
#include "cursiv/ll/runtime/ll_section.hpp"
#include "cursiv/ll/table/LlFuse.hpp"
#include "cursiv/ll/table/LlTable.hpp"
#include "cursiv/shared/runtime/LexByteRun.hpp"

namespace cursiv::engine {

// The token `mode` reads at `from`, short of the text's end: a one-byte
// literal off its byte alone, any other token through the walk.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline Scan ll_scan_text(const char* src, TextOffset len,
                                              std::uint16_t mode,
                                              TextOffset from) {
  if constexpr (T.byteTerms != nullptr) {
    const SymbolId t =
        T.byteTerms[T.modeBytes[mode] + static_cast<unsigned char>(src[from])];
    if (t != kInvalidSymbol) return Scan{from + 1, t};
  }
  return ll_match_text<T>(src, len, mode, from);
}

// The token the lexer matches at `from` under `mode`.
template <const auto& T>
inline Scan ll_scan(const LlContext& c, std::uint16_t mode, TextOffset from) {
  if constexpr (T.nsections != 0 && T.sectionEnd != nullptr)
    if (c.section != 0 &&
        T.sectionEnd[c.section - 1u] < T.nterms &&
        !T.mode_admits(mode, T.sectionEnd[c.section - 1u])) {
      const Scan end =
          ll_match_text<T>(c.src, c.len, ll_err_mode<T>(c), from);
      if (ll_ends<T>(c, end.term) ||
          (end.term < T.nterms && T.mode_covers(ll_err_mode<T>(c), mode) &&
           T.mode_admits(mode, end.term)))
        return end;
    }
  if (from >= c.len) return ll_match_text<T>(c.src, c.len, mode, from);
  return ll_scan_text<T>(c.src, c.len, mode, from);
}

// A set of bytes per mode, a member one load: modes whose sets agree read one
// table.
template <const auto& T, auto Rows>
inline constexpr auto kLlBytes =
    []<std::size_t... M>(std::index_sequence<M...>) {
      constexpr auto row = [](std::size_t m) {
        const std::uint64_t* const w = T.*Rows;
        return std::array<std::uint64_t, 4>{w[m * 4], w[m * 4 + 1], w[m * 4 + 2],
                                            w[m * 4 + 3]};
      };
      return std::array<const bool*, sizeof...(M)>{kInSet<row(M)>.data()...};
    }(std::make_index_sequence<T.nmodes>{});

// Past the run of blanks `mode` reads blind from `p` on: the walk would read
// it as unrecorded trivia.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline TextOffset ll_past_skip_run(const LlContext& c,
                                                        std::uint16_t mode,
                                                        TextOffset p) {
  if constexpr (T.skipSet == nullptr) {
    return p;
  } else {
    const bool* const blank = kLlBytes<T, &LlTable::skipSet>[mode];
    return lex_past_run(
        c.src, p, c.len, [&](unsigned char b) { return blank[b]; },
        [&]([[maybe_unused]] unsigned char b) {
          if constexpr (T.skipStop == nullptr)
            return false;
          else
            return kLlBytes<T, &LlTable::skipStop>[mode][b];
        });
  }
}

// Whether the read at the point may take a token in place, past the blanks:
// a section reads under its own alphabet first, and the indentation reads
// where each line starts.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline bool ll_in_place(const LlContext& c) {
  if constexpr (kIndented<T>)
    return false;
  else if constexpr (T.nsections != 0)
    return c.section == 0;
  else
    return true;
}

// Crosses the run of blanks `mode` reads blind at `p`, if one stands there.
// A section reads under its own alphabet first, so there the walk reads it.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline bool ll_crosses_skip_run(const LlContext& c,
                                           std::uint16_t mode, TextOffset& p) {
  if constexpr (T.nsections != 0)
    if (c.section != 0) return false;
  const TextOffset past = ll_past_skip_run<T>(c, mode, p);
  if (past == p) return false;
  p = past;
  return true;
}

// The gap from `p` on under `mode`, under the levels given, and the token behind
// it. `sc`, where it ends past `p`, is what the lexer read there already.
template <const auto& T, class Levels, class At, class Held, class Recorded>
CURSIV_ALWAYS_INLINE inline Token ll_cross_gap(const LlContext& c,
                                               const Levels& levels, At& at,
                                               Held& held, std::uint16_t mode,
                                               TextOffset& p, bool head, Scan sc,
                                               Recorded&& recorded) {
  for (;;) {
    if constexpr (kIndented<T>) {
      Token virtualToken;
      if (ll_indent_read<T>(c.src, c.len, levels, at, p, head, held, virtualToken,
                            ll_mode_ends<T>(mode)))
        return virtualToken;
    }
    if (p >= c.len) return Token{p, p, kEofSymbol, mode};
    if (ll_crosses_skip_run<T>(c, mode, p)) {
      head = false;
      continue;
    }
    if (sc.end <= p) sc = ll_scan<T>(c, mode, p);
    if (sc.term == kInvalidSymbol || sc.end == p || !ll_blank<T>(sc.term, at.depth))
      return Token{p, sc.end, sc.end == p ? kInvalidSymbol : sc.term, mode};
    if (T.terms[sc.term].recorded) recorded(Token{p, sc.end, sc.term});
    p = sc.end;
    head = false;
  }
}

// The gap from `p` on, and the token behind it in hand, under `mode`: the trivia
// that is recorded waits for the tape.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_read_gap(LlContext& c, std::uint16_t mode,
                                             TextOffset p, bool head, Scan sc) {
  c.la = ll_cross_gap<T>(c, c.indentArena, c.indent, c.heldEnd, mode, p, head, sc,
                         [&](const Token& t) { c.pending.push_back(t); });
  c.pendEnd = p;
  c.laValid = true;
  c.laMode = mode;
}

// The lookahead under one alphabet where the token in hand does not answer it:
// the token is read again where it begins, its gap kept; a token read as a
// blank joins the gap. `sc`: trivia already read.
template <const auto& T>
CURSIV_NOINLINE inline void ll_peek_read(LlContext& c, std::uint16_t mode,
                                         Scan sc = {}) {
  if constexpr (kIndented<T>)
    if (c.laValid) {
      // At the end of the input, the alphabet says whether a level closes
      // before the end, as a repair's read says it.
      if (c.la.sym == kEofSymbol &&
          ll_end_dedents<T>(c.indentArena, c.indent, ll_mode_ends<T>(mode)))
        c.la.sym = kSyntheticTerm<T, kSyntheticDedent>;
      else if (c.la.sym == kSyntheticTerm<T, kSyntheticDedent> &&
               c.la.begin == c.len && ll_mode_ends<T>(mode))
        c.la.sym = kEofSymbol;
      if (c.la.sym == kEofSymbol ||
          (c.la.sym < T.nterms && T.terms[c.la.sym].synthetic != 0)) {
        c.laMode = mode;
        return;
      }
    }
  TextOffset p = std::max(c.at, c.triviaAt);
  if (c.laValid && c.la.sym != kInvalidSymbol && c.la.sym != kEofSymbol) {
    p = c.la.begin;
    sc = ll_scan<T>(c, mode, p);
    if (sc.term == kInvalidSymbol || sc.end == p ||
        !ll_blank<T>(sc.term, c.indent.depth)) {
      c.la = Token{p, sc.end, sc.end == p ? kInvalidSymbol : sc.term, mode};
      c.laMode = mode;
      return;
    }
  } else {
    // The scan that ended the trivia is the token's own: reading it twice is
    // a second pass of the lexer over every token of the document.
    c.pending.clear();
  }
  ll_read_gap<T>(c, mode, p, p == c.at, sc);
}

// The token at the point, read in place where nothing is in hand: the blanks
// crossed blind, then the token, as the read would take it. A trivia goes on
// to the read with its scan; a token no terminal names is left to the read.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline bool ll_probe(LlContext& c, std::uint16_t mode) {
  if (!ll_in_place<T>(c)) return false;
  const TextOffset p = ll_past_skip_run<T>(c, mode, std::max(c.at, c.triviaAt));
  Token la{p, p, kEofSymbol, mode};
  if (p < c.len) {
    const Scan sc = ll_scan_text<T>(c.src, c.len, mode, p);
    if (sc.term == kInvalidSymbol || sc.end == p) return false;
    if (T.terms[sc.term].trivia) {
      ll_peek_read<T>(c, mode, sc);
      return true;
    }
    la.end = sc.end;
    la.sym = sc.term;
  }
  c.pending.clear();
  c.pendEnd = p;
  c.la = la;
  c.laValid = true;
  c.laMode = mode;
  return true;
}

// The token in hand stays the one the lexer chose, for every point that reads
// it on.
template <const auto& T>
inline void ll_hold(LlContext& c) {
  if (c.la.sym != kEofSymbol && c.la.sym != kInvalidSymbol &&
      !(c.la.sym < T.nterms && T.terms[c.la.sym].synthetic != 0))
    c.laMode = kLaHeld;
}

// The same, once a repair at the point is done: the repair reads under the
// widest alphabet, but the arm entered reads no terminal the point's lexer
// removes. `held` is the token held before, if any.
template <const auto& T>
inline void ll_rehold(LlContext& c, const Token& held, std::uint16_t mode) {
  if (held.sym == kInvalidSymbol || !c.laValid) return;
  if (c.la == held) {
    c.la = held;
  } else if (c.la.mode == kNoMode) {
    return;
  } else if (c.la.mode != mode && c.la.sym != kEofSymbol &&
             c.la.sym < T.nterms && T.terms[c.la.sym].synthetic == 0) {
    const Scan sc = ll_match_text<T>(c.src, c.len, mode, c.la.begin);
    c.la = Token{c.la.begin, sc.end,
                 sc.end == c.la.begin ? kInvalidSymbol : sc.term, mode};
  }
  ll_hold<T>(c);
}

// Whether the token held, read in its own alphabet, takes `term` away from it:
// it reserves the spelling, or it is a literal or a keyword and `term` reads
// the same bytes as a pattern.
template <const auto& T>
inline bool ll_held_takes(const LlContext& c, SymbolId term) {
  const LlLexTape<T> tape;
  auto state = tape.start(c.la.mode);
  for (auto i = c.la.begin; i < c.la.end && state != kDeadState; ++i)
    state = tape.step(state, static_cast<unsigned char>(c.src[i]));
  if (state == kDeadState) return false;
  TextOffset far = c.la.end;
  return tape.reserves(state, term, c.src, c.la.begin, c.la.end, c.len, far) ||
         (T.dfa_acc_set(state).test(term) &&
          tape.priority(state, c.la.sym, c.src, c.la.end, c.len, far) != 0 &&
          tape.priority(state, term, c.src, c.la.end, c.len, far) == 0);
}

// Whether `mode` lexes `sym` for the length of its token alone: no position
// reading in it takes `sym`, a condition standing there names it.
template <const auto& T>
constexpr bool ll_lengthens(std::uint16_t mode, std::uint16_t sym) {
  return T.has_longer() && mode < T.nmodes && T.longer_mode(mode) != kNoMode &&
         T.mode_admits(T.longer_mode(mode), sym);
}

// A held token answers a point under another alphabet with the terminal the
// lexer reads there, unless the held one removes it: another extent means a
// longer match of a terminal that cannot follow, or a condition's own token.
template <const auto& T>
inline void ll_peek_held(LlContext& c, std::uint16_t mode) {
  if (c.la.sym == kInvalidSymbol) return;
  if (T.mode_admits(mode, c.la.sym)) return;
  const Scan sc = ll_match_text<T>(c.src, c.len, mode, c.la.begin);
  if (sc.term == kInvalidSymbol || sc.end == c.la.begin) return;
  if (sc.end != c.la.end) {
    if (!ll_lengthens<T>(c.la.mode, c.la.sym))
      c.la = Token{c.la.begin, sc.end, sc.term, mode};
    return;
  }
  if (c.la.mode == kNoMode || !ll_held_takes<T>(c, sc.term)) c.la.sym = sc.term;
}

// The token as an implied token's position reads it, under that position's
// alphabet: what may follow an insertion is no candidate for the text its
// condition tests. A token no terminal names is read again there too.
template <const auto& T>
inline Token ll_normal_token(const LlContext& c, const Token& token,
                             std::uint16_t mode) {
  if (token.sym < T.nterms && T.mode_admits(mode, token.sym)) return token;
  const Scan sc = ll_match_text<T>(c.src, c.len, mode, token.begin);
  if (sc.term == kInvalidSymbol || sc.end == token.begin) return token;
  return Token{token.begin, sc.end, sc.term, mode};
}

template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_peek(LlContext& c, std::uint16_t mode) {
  if (c.laValid && c.laMode == mode) [[likely]]
    return;
  if (c.laValid && c.laMode == kLaHeld) {
    ll_peek_held<T>(c, mode);
    return;
  }
  // A token matched under a wider alphabet is the token a narrower one
  // reads, as long as the narrower admits the terminal it matched: no
  // subset of the candidates can match further.
  if (c.laValid && c.la.sym != kInvalidSymbol) {
    bool physical = true;
    if constexpr (kIndented<T>)
      physical = c.la.sym != kEofSymbol && c.la.sym < T.nterms &&
                 T.terms[c.la.sym].synthetic == 0;
    if (physical &&
        (ll_ends<T>(c, c.la.sym) ||
         (T.mode_covers(c.laMode, mode) && T.mode_admits(mode, c.la.sym)))) {
      c.laMode = mode;
      return;
    }
  }
  ll_peek_read<T>(c, mode);
}

// The token at a point of the generated descent, which names its alphabet:
// where nothing is in hand, read in place.
template <const auto& T, std::uint16_t Mode>
CURSIV_ALWAYS_INLINE inline void ll_peek(LlContext& c) {
  if (!c.laValid && ll_probe<T>(c, Mode)) [[likely]]
    return;
  ll_peek<T>(c, Mode);
}

// A repetition may peek at its closing token across its caller's trivia; once
// it elects another item, an uncommitted gap is read again under the item's
// configuration. Tokens in place and invented tokens stay.
template <const auto& T>
inline void ll_repeat_read(LlContext& c, std::uint16_t mode) {
  if (!c.laValid || c.la.mode == kNoMode ||
      c.la.begin <= std::max(c.at, c.triviaAt))
    return;
  const bool held = c.laMode == kLaHeld;
  c.laValid = false;
  c.laMode = kInvalidSymbol;
  c.pending.clear();
  ll_peek<T>(c, mode);
  // A forbidden gap must reach the body's repair, even if that body's first
  // position also serves a caller with more permissive trivia.
  if (c.la.sym == kInvalidSymbol)
    c.laMode = kLaHeld;
  else if (held)
    ll_hold<T>(c);
}

// The terminals a position is read without.
template <const auto& T>
using LlSkipped = typename LlLexTape<T>::Skip;

// A read that a refusal may give back: the token in hand and the trivia read in
// front of it.
struct LlHeldRead {
  Token la;
  std::uint16_t mode;
  std::size_t pending;
  TextOffset pendEnd;
};
inline LlHeldRead ll_hold_read(const LlContext& c) {
  return {c.la, c.laMode, c.pending.size(), c.pendEnd};
}
inline void ll_give_back(LlContext& c, const LlHeldRead& read) {
  c.la = read.la;
  c.laMode = read.mode;
  c.pending.resize(read.pending);
  c.pendEnd = read.pendEnd;
}

// The token in hand read again under `mode`, its terminal added to `skip`;
// trivia that reads there instead is crossed. False, nothing changed, where the
// token is no terminal's or nothing reads there.
template <const auto& T>
inline bool ll_read_without(LlContext& c, std::uint16_t mode,
                            LlSkipped<T>& skip) {
  if (c.la.sym == kEofSymbol || c.la.sym >= T.nterms ||
      T.terms[c.la.sym].synthetic != 0)
    return false;
  skip.set(c.la.sym);
  const typename LlLexTape<T>::Skipping tape{.skip = &skip};
  const Scan sc = lex_match(tape, c.src, c.len, mode, c.la.begin);
  if (sc.term == kInvalidSymbol || sc.end == c.la.begin) return false;
  if (!ll_blank<T>(sc.term, c.indent.depth)) {
    c.la = Token{c.la.begin, sc.end, sc.term, mode};
    c.laMode = mode;
    return true;
  }
  const LlHeldRead held = ll_hold_read(c);
  if (T.terms[sc.term].recorded)
    c.pending.push_back(Token{c.la.begin, sc.end, sc.term});
  skip = {};
  ll_read_gap<T>(c, mode, sc.end, false, Scan{});
  if (c.la.sym != kInvalidSymbol) return true;
  ll_give_back(c, held);
  return false;
}

// The terminals that tie with the token in hand, itself first, under what
// `skip` took off the position. A terminal a refused keyword reserved reads
// nowhere in the mode's own table, so each is read with the ones before it off.
template <const auto& T>
inline void ll_ties_without(const LlContext& c, std::uint16_t mode,
                            LlSkipped<T> skip,
                            std::vector<std::uint16_t>& ties) {
  ties.assign(1, c.la.sym);
  // A read past a trivia may stand on the end, or on a token nothing lexes.
  if (c.la.sym == kEofSymbol || c.la.sym >= T.nterms ||
      T.terms[c.la.sym].synthetic != 0)
    return;
  const typename LlLexTape<T>::Skipping tape{.skip = &skip};
  const Scan own = lex_match(tape, c.src, c.len, mode, c.la.begin);
  for (Scan at = own;;) {
    skip.set(at.term);
    at = lex_match(tape, c.src, c.len, mode, c.la.begin);
    if (at.term == kInvalidSymbol || at.end != own.end ||
        lex_weight(at.priority) != lex_weight(own.priority) ||
        ll_blank<T>(at.term, c.indent.depth))
      return;
    ties.push_back(at.term);
  }
}

// The token in hand at a choice whose alphabet admits, for a caller only, a
// terminal that reads over another: where no frame goes on from it, the
// position is read again without that longer match.
template <const auto& T>
CURSIV_NOINLINE inline void ll_refuse_flagged(LlContext& c, std::uint16_t mode,
                                              std::uint16_t decision) {
  LlSkipped<T> skip{};
  while (T.holds(T.dec_flag(decision), c.la.sym) &&
         !ll_callers_take<T>(c, c.la.sym) && ll_read_without<T>(c, mode, skip)) {
  }
}

// A read ahead of the descent, from where the token in hand was read: the
// trivia before it is crossed, and not read again under another alphabet. Its
// levels are the descent's, in the descent's arena.
inline LlAhead ll_ahead(const LlContext& c) {
  return LlAhead{c.indent, c.heldEnd, c.pendEnd, c.pendEnd == c.at};
}

// The next visible token of a read ahead, as the descent would read it under
// `mode`: trivia crossed, the indentation said where a line starts.
// `kInvalidSymbol` where nothing reads, `kEofSymbol` at the end of the text.
template <const auto& T>
inline Token ll_ahead_peek(const LlContext& c,
                           const std::vector<IndentLevel>& levels, LlAhead& a,
                           std::uint16_t mode) {
  TextOffset p = a.p;
  const Token t = ll_cross_gap<T>(c, levels, a.at, a.held, mode, p, a.head, Scan{},
                                  [](const Token&) {});
  // Only a token that reads holds a mode, and an end.
  return t.sym == kInvalidSymbol || t.sym == kEofSymbol
             ? Token{t.begin, t.begin, t.sym}
             : t;
}

// Steps a read ahead past `t`, taken or dropped as a repair drops it.
template <const auto& T>
inline void ll_ahead_step(std::vector<IndentLevel>& levels, LlAhead& a,
                          const Token& t, bool dropped) {
  if constexpr (kIndented<T>) {
    if (dropped)
      indent_drop(levels, a.at.head, t, kSyntheticTerm<T, kSyntheticIndent>,
                  kSyntheticTerm<T, kSyntheticDedent>);
    else
      indent_shift<T>(levels, a.at, t);
    if (t.end != t.begin) a.held = t.end;
  }
  a.p = t.end;
  a.head = true;
}

// The trivia in front of the token goes on the tape first: a record's end
// never decreases, so a hidden run written after the token it precedes would
// put the tree out of order.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_flush_trivia(LlContext& c) {
  for (const Token& t : c.pending)
    c.tape.hidden(t.begin, t.end, T.stamp.make(T.terms[t.sym].element, 0));
  c.pending.clear();
  c.triviaAt = std::max(c.triviaAt, c.pendEnd);
}

// One token onto the tape, and the cursor past it. `row` is everything the
// leaf writes: the build joined the terminal's own conversion to whatever its
// position states, so nothing is decided here.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_consume(LlContext& c, std::uint16_t row,
                                            std::uint16_t pos = 0) {
  if (c.run.open && c.run.del && c.run.at == c.at)
    c.run.resume = c.la.sym;
  ll_flush_trivia<T>(c);
  c.last = c.tape.leaf(c.la.begin, c.la.end, T.stamp.make(row, pos));
  if constexpr (kIndented<T>) {
    indent_shift<T>(c.indentArena, c.indent, c.la);
    if (c.la.end != c.la.begin) {
      c.heldEnd = c.la.end;
      c.zeroWidth = 0;
    } else {
      ++c.zeroWidth;
    }
  }
  c.at = c.la.end;
  c.invented = 0;
  c.laValid = false;
  c.laMode = kInvalidSymbol;
}

// What a call site states over the record its call returned, a join the build
// interned. `was` is the record before the call: a call that returned none, or
// gave up with an empty record, leaves it standing with nothing to state.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_join(LlContext& c, NodeId was,
                                         const LlFuse* f, unsigned n) {
  if (c.last == was || c.last == kNoNode) return;
  CstNode& r = c.tape.stream().at(c.last);
  const ElementId h = T.stamp.row(r);
  if (r.recovered && h == kNoElement) [[unlikely]]
    return;
  for (unsigned i = 0; i < n; ++i)
    if (f[i].from == h) {
      T.stamp.set_row(r, f[i].to);
      return;
    }
}

// Whether record `at` is trivia, or a repair whose subtree names no row.
template <const auto& T>
inline bool ll_states_nothing(const CstRun& run, NodeCount at) {
  const CstNode& r = run.at(at);
  if (r.hidden) return true;
  if (!r.recovered) return false;
  for (NodeCount k = at + 1 - r.subtreeSize; k <= at; ++k)
    if (!run.at(k).hidden && T.stamp.row(run.at(k)) != kNoElement)
      return false;
  return true;
}

// A group whose content is one subtree covering it: the chain goes onto the
// subtree instead of a second record. False with more than one position, two
// creations on one record, or a row an elected abandon erased.
template <const auto& T>
inline bool ll_reuse_subtree(LlContext& c, NodeCount from, const LlFuse* f,
                             unsigned n) {
  if (n == 0 || c.last == kNoNode) return false;
  CstRun& run = c.tape.stream();
  NodeCount now = run.records();
  if (c.last + 1u != now) [[unlikely]] {
    while (now > c.last + 1u && ll_states_nothing<T>(run, now - 1))
      now -= run.at(now - 1).subtreeSize;
    if (c.last + 1u != now) return false;
  }
  if (now == from) return false;
  CstNode& r = run.at(c.last);
  if (now - from != r.subtreeSize) [[unlikely]] {
    if (!c.repaired || now - from < r.subtreeSize) return false;
    for (NodeCount k = now - r.subtreeSize; k > from;
         k -= run.at(k - 1).subtreeSize)
      if (!ll_states_nothing<T>(run, k - 1)) return false;
  }
  const ElementId h = T.stamp.row(r);
  for (unsigned i = 0; i < n; ++i)
    if (f[i].from == h) {
      if (h == kNoElement && c.abandonDepth != ~std::size_t{0}) return false;
      T.stamp.set_row(r, f[i].to);
      if (c.give) {
        r.recovered = true;
        c.repaired.mark();
      }
      return true;
    }
  return false;
}

template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_join_one(LlContext& c, NodeId was,
                                             ElementId to) {
  if (c.last == was || c.last == kNoNode) return;
  CstNode& r = c.tape.stream().at(c.last);
  if (r.recovered && T.stamp.row(r) == kNoElement) [[unlikely]]
    return;
  T.stamp.set_row(r, to);
}

}  // namespace cursiv::engine
