// Reading, and the two writes a read owes the tree: trivia goes on the tape
// in front of the token it precedes, and the token's own leaf carries the row
// its position states.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/ll/runtime/LlAhead.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_indent.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/ll/table/LlFuse.hpp"
#include "cursiv/engines/ll/table/LlTable.hpp"
#include "cursiv/engines/shared/runtime/LexByteRun.hpp"

namespace cursiv {

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

// The blank bytes of each mode, a member one load: modes whose sets agree
// read one table.
template <const auto& T>
inline constexpr auto kLlBlanks =
    []<std::size_t... M>(std::index_sequence<M...>) {
      constexpr auto row = [](std::size_t m) {
        return std::array<std::uint64_t, 4>{
            T.skipSet[m * 4], T.skipSet[m * 4 + 1], T.skipSet[m * 4 + 2],
            T.skipSet[m * 4 + 3]};
      };
      return std::array<const bool*, sizeof...(M)>{kInSet<row(M)>.data()...};
    }(std::make_index_sequence<T.nmodes>{});

// Past the run of blanks `mode` reads blind from `p` on: the walk would read
// it as one unrecorded trivia token.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline TextOffset ll_past_blanks(const LlContext& c,
                                                      std::uint16_t mode,
                                                      TextOffset p) {
  if constexpr (T.skipSet != nullptr) {
    const bool* const blank = kLlBlanks<T>[mode];
    while (p < c.len && blank[static_cast<unsigned char>(c.src[p])]) ++p;
  }
  return p;
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
CURSIV_ALWAYS_INLINE inline bool ll_blanks(const LlContext& c,
                                           std::uint16_t mode, TextOffset& p) {
  if constexpr (T.nsections != 0)
    if (c.section != 0) return false;
  const TextOffset past = ll_past_blanks<T>(c, mode, p);
  if (past == p) return false;
  p = past;
  return true;
}

// The lookahead under one alphabet, where the token in hand does not answer
// it. A token already read under another is read again: the two alphabets
// may not agree on where it ends, and the point that asks is the one that
// decides. The indentation's tokens stand under any alphabet, and so does
// the end, a dedent where `mode` owes one. `sc`, where it ends past the
// blanks, is the trivia the lexer read there already.
template <const auto& T>
CURSIV_NOINLINE inline void ll_peek_read(LlContext& c, std::uint16_t mode,
                                         Scan sc = {}) {
  if constexpr (kIndented<T>)
    if (c.laValid) {
      if (c.la.sym == kEofSymbol &&
          ll_end_dedents<T>(c.indentArena, c.indent, ll_mode_ends<T>(mode)))
        c.la.sym = kLaid<T, kLaidDedent>;
      if (c.la.sym == kEofSymbol ||
          (c.la.sym < T.nterms && T.terms[c.la.sym].laid != 0)) {
        c.laMode = mode;
        return;
      }
    }
  TextOffset p = std::max(c.at, c.triviaAt);
  // THE SCAN THAT ENDED THE TRIVIA IS THE TOKEN'S OWN: reading it twice is a
  // second pass of the lexer over every token of the document.
  c.pending.clear();
  bool head = p == c.at;
  for (;;) {
    if constexpr (kIndented<T>) {
      Token virtualToken;
      if (ll_indent_read<T>(c.src, c.len, c.indentArena, c.indent, p, head,
                            c.heldEnd, virtualToken, ll_mode_ends<T>(mode))) {
        c.pendEnd = p;
        c.la = virtualToken;
        c.laValid = true;
        c.laMode = mode;
        return;
      }
    }
    if (p >= c.len) break;
    if (ll_blanks<T>(c, mode, p)) {
      head = false;
      continue;
    }
    if (sc.end <= p) sc = ll_scan<T>(c, mode, p);
    if (sc.term == kInvalidSymbol || sc.end == p) break;
    const auto& term = T.terms[sc.term];
    bool blank = term.trivia;
    if constexpr (kIndented<T>)
      blank = blank || (term.joins && c.indent.depth != 0);
    if (!blank) break;
    if (term.recorded) c.pending.push_back(Token{p, sc.end, sc.term});
    p = sc.end;
    head = false;
  }
  c.pendEnd = p;
  c.la = p >= c.len
             ? Token{p, p, kEofSymbol, mode}
             : Token{p, sc.end, sc.end == p ? kInvalidSymbol : sc.term, mode};
  c.laValid = true;
  c.laMode = mode;
}

// The token at the point, read in place where nothing is in hand: the blanks
// crossed blind, then the token, as the read would take it. A trivia goes on
// to the read with its scan; a token no terminal names is left to the read.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline bool ll_probe(LlContext& c, std::uint16_t mode) {
  if (!ll_in_place<T>(c)) return false;
  const TextOffset p = ll_past_blanks<T>(c, mode, std::max(c.at, c.triviaAt));
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
      !(c.la.sym < T.nterms && T.terms[c.la.sym].laid != 0))
    c.laMode = kLaHeld;
}

// ...and again once a repair at the point is done: the repair reads under the
// widest alphabet, yet the arm entered reads no terminal the point's lexer
// takes away. `held` is the token held before, none where none was; a token
// the repair spelt itself stays its own.
template <const auto& T>
inline void ll_rehold(LlContext& c, const Token& held, std::uint16_t mode) {
  if (held.sym == kInvalidSymbol || !c.laValid) return;
  if (c.la == held) {
    c.la = held;
  } else if (c.la.mode == kNoMode) {
    return;
  } else if (c.la.mode != mode && c.la.sym != kEofSymbol &&
             c.la.sym < T.nterms && T.terms[c.la.sym].laid == 0) {
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
          tape.tier(state, c.la.sym, c.src, c.la.end, c.len, far) != 0 &&
          tape.tier(state, term, c.src, c.la.end, c.len, far) == 0);
}

// A token held answers a point that reads under another alphabet with the
// terminal the lexer reads there, unless the held one takes it away. The
// alphabet that does not admit it is the narrower one: where it reads another
// extent, the held token was a longer match of a terminal that cannot follow,
// unless it is a condition's own token, which lengthens what it guards.
template <const auto& T>
inline void ll_peek_held(LlContext& c, std::uint16_t mode) {
  if (T.mode_admits(mode, c.la.sym)) return;
  const Scan sc = ll_match_text<T>(c.src, c.len, mode, c.la.begin);
  if (sc.term == kInvalidSymbol || sc.end == c.la.begin) return;
  if (sc.end != c.la.end) {
    if (T.terms[c.la.sym].element != kNoElement)
      c.la = Token{c.la.begin, sc.end, sc.term, mode};
    return;
  }
  if (c.la.mode == kNoMode || !ll_held_takes<T>(c, sc.term)) c.la.sym = sc.term;
}

// The token as the position of an implied token reads it, under that
// position's own alphabet: what may follow an insertion is no candidate for
// the text its condition tests. A token no terminal names, where the lexer
// read nothing, is read again there too.
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
                 T.terms[c.la.sym].laid == 0;
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

// A read ahead of the descent, from the token in hand: the trivia before it
// is crossed, and not read again under another alphabet. Its levels are the
// descent's, in the descent's arena.
inline LlAhead ll_ahead(const LlContext& c) {
  return LlAhead{c.indent, c.heldEnd, c.la.begin, c.la.begin == c.at};
}

// The next visible token of a read ahead, as the descent would read it under
// `mode`: trivia crossed, the indentation said where a line starts.
// `kInvalidSymbol` where nothing reads, `kEofSymbol` at the end of the text.
template <const auto& T>
inline Token ll_ahead_peek(const LlContext& c,
                           const std::vector<IndentLevel>& levels, LlAhead& a,
                           std::uint16_t mode) {
  TextOffset p = a.p;
  bool head = a.head;
  for (;;) {
    if constexpr (kIndented<T>) {
      Token virtualToken;
      if (ll_indent_read<T>(c.src, c.len, levels, a.at, p, head, a.held,
                            virtualToken, ll_mode_ends<T>(mode)))
        return virtualToken;
    }
    if (p >= c.len) return Token{p, p, kEofSymbol};
    if (ll_blanks<T>(c, mode, p)) {
      head = false;
      continue;
    }
    const Scan sc = ll_scan<T>(c, mode, p);
    if (sc.term == kInvalidSymbol || sc.end == p)
      return Token{p, p, kInvalidSymbol};
    bool blank = T.terms[sc.term].trivia;
    if constexpr (kIndented<T>)
      blank = blank || (T.terms[sc.term].joins && a.at.depth != 0);
    if (!blank) return Token{p, sc.end, sc.term, mode};
    p = sc.end;
    head = false;
  }
}

// ...and past `t`, taken or dropped as a repair drops it.
template <const auto& T>
inline void ll_ahead_step(std::vector<IndentLevel>& levels, LlAhead& a,
                          const Token& t, bool dropped) {
  if constexpr (kIndented<T>) {
    if (dropped)
      indent_drop(levels, a.at.head, t, kLaid<T, kLaidIndent>,
                  kLaid<T, kLaidDedent>);
    else
      indent_shift<T>(levels, a.at, t);
    if (t.end != t.begin) a.held = t.end;
  }
  a.p = t.end;
  a.head = true;
}

// The trivia in front of the token goes on the tape first: a record's end
// never decreases, so a hidden run laid after the token it precedes would put
// the tree out of order.
template <const auto& T>
CURSIV_ALWAYS_INLINE inline void ll_flush_trivia(LlContext& c) {
  for (const Token& t : c.pending)
    c.tape.hidden(t.begin, t.end, T.stamp.make(T.terms[t.sym].element, 0));
  c.pending.clear();
  c.triviaAt = std::max(c.triviaAt, c.pendEnd);
}

// One token onto the tape, and the cursor past it. `row` is the whole of what
// the leaf owes: the build joined the terminal's own conversion to whatever
// its position states, so nothing is decided here.
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
      c.lull = 0;
    } else {
      ++c.lull;
    }
  }
  c.at = c.la.end;
  c.invented = 0;
  c.laValid = false;
  c.laMode = kInvalidSymbol;
}

// What a call site states over the record its call handed back. The joins
// were interned by the build, so this is a lookup and never a construction --
// and a site whose call always hands the same record has one of them, which
// the generated code writes without a test.
//
// `was` is the record in hand before the call: a call that handed none back
// -- a repair refused it the room to write one, the descent was too deep, or
// it laid nothing but the trivia in front of it -- leaves that record
// standing, and the site has nothing of its own to state over -- nor where a
// call that gave up left its record saying nothing.
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

// A group whose content turned out to be one subtree covering all of it: a
// record over it would span exactly what that one spans, so the chain rides
// onto it and no second record is laid. False where the text gave the group
// more than one position, or where riding would put two creations on one
// record -- the build offers no join there -- or where a join from no row
// meets the row an elected abandon erased: it would put the group's creation
// back, where ll_close lays the group's record with none. A local give-up
// marks the ride as ll_close marks its record. Trivia, and repairs that
// state nothing, may follow the subtree: they owe the group nothing.
template <const auto& T>
inline bool ll_ride(LlContext& c, NodeCount from, const LlFuse* f, unsigned n) {
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
  if (now - from != r.subtreeSize) return false;
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

}  // namespace cursiv
