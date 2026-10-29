// Table readers shared by lookahead, recovery and rescan.
#pragma once

#include <array>
#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/lr_guard.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/lr/runtime/lr_moves.hpp"
#include "cursiv/engines/lr/table/LrBitSet.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// Whether any production waits on a condition of the gap.
template <const auto& A>
inline constexpr bool kLrAnyGap = [] {
  for (unsigned p = 0; p < A.cfg.nprods; ++p)
    if (A.cfg.prods[p].gap != 0) return true;
  return false;
}();

// The alphabet as bits, so telling trivia apart is an immediate.
template <const auto& A, bool Recorded>
inline constexpr LrBitSet<A.cfg.nterms> kLrTermMask = [] {
  LrBitSet<A.cfg.nterms> m{};
  for (unsigned t = 0; t < A.cfg.nterms; ++t)
    if (Recorded ? A.cfg.terms[t].recorded : A.cfg.terms[t].trivia) m.set(t);
  return m;
}();

// The implied terminals by rank: a reading's `inserted` holds a bit each.
template <const auto& A>
inline constexpr auto kLrImplied = [] {
  constexpr unsigned n = [] {
    unsigned k = 0;
    for (unsigned t = 1; t < A.cfg.nterms; ++t)
      k += A.cfg.terms[t].laid == kLaidImplied;
    return k;
  }();
  std::array<SymbolId, n> q{};
  for (unsigned t = 1, k = 0; t < A.cfg.nterms; ++t)
    if (A.cfg.terms[t].laid == kLaidImplied) q[k++] = static_cast<SymbolId>(t);
  return q;
}();

// Whether a reading carries what conditions ask of the gap: an implied token,
// a production waiting on the gap, or a guard.
template <const auto& A>
inline constexpr bool kLrConditioned =
    kLrImplied<A>.size() != 0 || kLrAnyGap<A> || kLrGuarded<A>;

// Inside a bracket a token that beat a trivia at its extent is that trivia:
// `depth` is the bracket depth of the reading that reads it.
template <const auto& A>
CURSIV_ALWAYS_INLINE inline bool lr_joins(std::uint32_t depth, unsigned t) {
  if constexpr (kIndented<A.cfg>)
    return depth != 0 && A.cfg.terms[t].joins;
  else
    return false;
}

template <const auto& A>
CURSIV_ALWAYS_INLINE inline bool lr_is_trivia(unsigned t) {
  return kLrTermMask<A, false>.test(t);
}

// The token at `from` under one alphabet, trivia crossed and not recorded,
// read by the drive's own walk under bracket depth `depth`: of the levels of
// the reading that asks, a scan reads that depth alone. `Short` reads the text
// as ending at `end` with its inspection frontier: `refused`, whether the walk
// that read it read on past its match and died, `reached` rising to how far
// the walks read. Out of line, as the lexer's walk is: inlined into a
// reader's loop, it costs every step of it.
template <const auto& A, bool Short = false>
CURSIV_NOINLINE inline Token lr_scan_at(const LrContext& c, std::uint16_t mode,
                        TextOffset from, std::uint32_t depth,
                        bool* refused = nullptr, TextOffset end = 0,
                        TextOffset* reached = nullptr) {
  const TextOffset len = Short ? end : c.len;
  TextOffset p = from;
  for (;;) {
    while (p < len && lr_skips<A>(mode, static_cast<unsigned char>(c.src[p])))
      ++p;
    if (p >= len) {
      if constexpr (Short) *reached = p;
      return Token{p, p, kEofSymbol};
    }
    TextOffset halt = p;
    Scan s = Short ? lr_match_halt<A>(c.src, len, mode, p, halt)
                   : c.drive->walk(c.src, len, mode, p);
    if constexpr (Short) {
      if (halt > *reached) *reached = halt;
      if (refused != nullptr) *refused = halt > s.end;
    }
    // Where the ranked acceptance's boundary refuses it, the terminals it
    // outranked live in the sweep alone, and the drive reads on from there.
    if (s.term == kInvalidSymbol || s.end == p) {
      if constexpr (A.maxCands > 1) {
        ScanRoom<A.maxCands> room;
        if (c.drive->scanText != nullptr &&
            c.drive->scanText(c.src, len, mode, p, room.data(), A.maxCands) !=
                0)
          s = room[0];
      }
      if (s.term == kInvalidSymbol || s.end == p)
        return Token{p, p, kInvalidSymbol};
    }
    if (!lr_is_trivia<A>(s.term) && !lr_joins<A>(depth, s.term))
      return Token{p, s.end, s.term, mode};
    p = s.end;
  }
}

// Whether the lexer, choosing once for every parse at one place, elects `u`
// over `t`: the longer, then a literal or a keyword over a pattern of the same
// bytes. `heavier` weighs `u` against `t`; only two terminals of one extent
// ask it.
template <class Heavier>
inline bool lr_token_beats(const Token& u, const Token& t, Heavier&& heavier) {
  if (u.begin != t.begin || u.end < t.end) return false;
  if (u.end > t.end) return true;
  if (u.sym == t.sym || u.mode == kNoMode || t.mode == kNoMode) return false;
  return heavier();
}

// A token's weight, read again in the alphabet it was read in.
template <const auto& A>
inline unsigned lr_token_weight(const LrContext& c, const Token& x) {
  return lex_weight(lex_reading_tier(LrLexTape<A>{}, c.src, c.len, x.mode,
                                    x.begin, x.end, x.sym));
}

template <const auto& A>
inline bool lr_token_beats(const LrContext& c, const Token& u, const Token& t) {
  return lr_token_beats(u, t, [&] {
    return lr_token_weight<A>(c, u) > lr_token_weight<A>(c, t);
  });
}

template <const auto& A>
CURSIV_ALWAYS_INLINE inline bool lr_has_rivals(std::uint16_t mode,
                                               SymbolId sym) {
  if constexpr (A.maxCands > 1)
    return sym < A.cfg.nterms && A.modeWin[mode].test(sym);
  else
    return false;
}

// The terminals matching at `begin`, trivia withdrawn, in the order the
// deterministic matcher would have answered.
template <const auto& A>
CURSIV_ALWAYS_INLINE inline std::uint32_t lr_rivals(const LrContext& c,
                                                    std::uint16_t mode,
                                                    TextOffset begin,
                                                    Scan* room) {
  const std::uint32_t n =
      c.drive->scanText(c.src, c.len, mode, begin, room, A.maxCands);
  std::uint32_t keep = 0;
  for (std::uint32_t q = 0; q < n; ++q)
    if (!lr_is_trivia<A>(room[q].term)) room[keep++] = room[q];
  return keep;
}

// Whether the rivals do not weigh alike: only then a derivation owes the word
// its token's rank.
CURSIV_ALWAYS_INLINE inline bool lr_rivals_ranked(const Scan* room,
                                                  std::uint32_t n) {
  for (std::uint32_t q = 1; q < n; ++q)
    if (lex_weight(room[q].tier) != lex_weight(room[0].tier)) return true;
  return false;
}

// The token after the lookahead, under one mode, and nothing moves. Past the
// token in hand the levels may have moved: at a line start the peek gives no
// answer.
template <const auto& A>
SymbolId lr_peek2(const LrContext& c, std::uint16_t mode) {
  if constexpr (kIndented<A.cfg>)
    if (indent_moves<A.cfg>(c.la.sym) ||
        (c.indent.depth == 0 && c.la.end < c.len &&
         indent_line_start(c.src, c.la.end, [&c] { return c.la.end; })))
      return kInvalidSymbol;
  return lr_scan_at<A>(c, mode, c.la.end, c.indent.depth).sym;
}

// Whether any terminal is one at all: where none is, `pending` is never
// filled, and the flush is the cursor move alone.
template <const auto& A>
inline constexpr bool kLrAnyRecorded = kLrTermMask<A, true>.any();

// The row each terminal's record states.
template <const auto& A>
inline constexpr std::array<ElementId, A.cfg.nterms> kLrTermElem = [] {
  std::array<ElementId, A.cfg.nterms> e{};
  for (unsigned t = 0; t < A.cfg.nterms; ++t) e[t] = A.cfg.terms[t].element;
  return e;
}();

template <const auto& A>
inline void lr_flush_trivia(LrContext& c) {
  lr_flush<kLrAnyRecorded<A>, A.stamp>(c, kLrTermElem<A>.data());
}

template <const auto& A>
inline void lr_action_run(std::uint32_t state, SymbolId term,
                          std::uint32_t& first, unsigned& count) {
  const auto cell = A.programs.cell(state, term);
  first = A.programs.first(cell);
  count = A.programs.count(cell);
}

template <const auto& A>
constexpr std::uint32_t lr_goto_of(std::uint32_t from, SymbolId nt) {
  return A.gotoPrograms.get(from, nt);
}

// Transition decisions do not change a reading, its stack or its records.
template <const auto& A, class Holds>
CURSIV_ALWAYS_INLINE constexpr bool lr_reduce_allowed(
    std::uint16_t prod, Holds&& holds) {
  if constexpr (kLrAnyGap<A>) {
    const unsigned gap = A.cfg.prods[prod].gap;
    return gap == 0 || holds(gap - 1u);
  } else {
    (void)prod, (void)holds;
    return true;
  }
}

template <const auto& A, class Holds>
CURSIV_ALWAYS_INLINE constexpr std::uint32_t lr_shift_target(
    LrActionKind kind, std::uint32_t arg, Holds&& holds) {
  if constexpr (kLrGuarded<A>)
    if (kind == LrActionKind::guard) return lr_guard_land<A>(arg, holds);
  (void)kind, (void)holds;
  return arg;
}

// The caller supplies the states already found on its own stack or pop path.
template <const auto& A, class Holds>
CURSIV_ALWAYS_INLINE constexpr std::uint32_t lr_reduce_target(
    std::uint32_t below, std::uint16_t prod, std::uint32_t first,
    Holds&& holds) {
  return lr_goto_land<A>(lr_goto_of<A>(below, A.cfg.prods[prod].lhs), first,
                         holds);
}

// A guarded goto lands out of line, so the plain reduce stays as small as a
// grammar without conditions makes it.
template <const auto& A, class Climb, class Holds>
CURSIV_NOINLINE std::uint32_t lr_apply_guarded_reduce(Climb& climb,
                                                      std::uint16_t p,
                                                      Holds&& holds) {
  const auto& prod = A.cfg.prods[p];
  std::uint32_t first = kLrNoFirst;
  if (prod.len != 0) first = climb.state_at(climb.size() - prod.len);
  climb.pop(prod.len);
  const std::uint32_t s = lr_reduce_target<A>(climb.back(), p, first, holds);
  if (s != kInvalidSymbol) climb.push(s);
  return s;
}

// Close production `p` over `climb`: pop its RHS, follow the goto it lands
// on -- a guarded one by the tag of the span's first state, or by `holds`
// where the span is empty. `kInvalidSymbol` where the climb cannot continue.
template <const auto& A, class Climb, class Holds>
CURSIV_ALWAYS_INLINE inline std::uint32_t lr_apply_reduce(Climb& climb,
                                                          std::uint16_t p,
                                                          Holds&& holds) {
  const auto& prod = A.cfg.prods[p];
  if (prod.len + 1u > climb.size()) return kInvalidSymbol;
  if constexpr (kLrGuarded<A>)
    if (prod.guarded != 0) [[unlikely]]
      return lr_apply_guarded_reduce<A>(climb, p, holds);
  climb.pop(prod.len);
  const std::uint32_t s = lr_goto_of<A>(climb.back(), prod.lhs);
  if (s != kInvalidSymbol) climb.push(s);
  return s;
}

}  // namespace cursiv
