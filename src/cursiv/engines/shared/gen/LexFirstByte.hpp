// What a lexer reads off a token's first byte without its walk, for every
// engine: the blank runs it crosses blind, and the one-byte literals it takes
// whole. Also the part of the automaton's view both engines share.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/engines/shared/table/LexRelay.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// `Dfa` names one engine's automaton: `modes`, `start(m)`, `classes`,
// `cls(byte)`, `next(s, cls)`, `accept(s)` (the terminal plus one, zero where
// it accepts nothing), `bounded(s)` (whether its election reads past the
// token), `ends(s)` where `bounded` leaves it out (whether an acceptance
// stands only where no byte follows), `relay(s)` (`kNoRelay` where it holds
// none), and per terminal `term(t)`, `text(t)` and `folded(t)`.

// The part of `Dfa` both engines' tables hold under the same names, with
// `relay_of(k)`: an engine's view adds `bounded`, `relay` and `tier`.
template <class Table>
struct LexTableView {
  const Table& a;

  std::uint32_t modes() const { return a.nmodes; }
  std::uint16_t start(std::uint32_t m) const { return a.modeStart[m]; }
  unsigned classes() const { return a.ncls; }
  std::uint8_t cls(unsigned b) const { return a.cls[b]; }
  std::uint16_t next(std::uint16_t s, unsigned c) const {
    return a.dfaNext[std::size_t{s} * a.ncls + c];
  }
  std::uint16_t accept(std::uint16_t s) const { return a.dfaAccept[s]; }
  const auto& relay_of(std::uint16_t k) const { return a.relays[k]; }
  const TermInfo& term(unsigned t) const { return a.cfg.terms[t]; }
  std::string_view text(unsigned t) const {
    return a.cfg.term_text(static_cast<std::uint16_t>(t));
  }
  bool folded(unsigned t) const { return a.cfg.termBuild[t].folded; }
};

// The bytes an ignored run is made of, four words per mode: the mode's start
// takes the byte to an unrecorded trivia token whose every live edge comes
// back to it. A trivia that refuses words behind its head stays with the
// matcher, which asks them, and so does every run of a mode whose start
// launches a relay: the token it reads may begin with that run.
template <class Dfa>
std::vector<std::uint64_t> lex_skip_set(const Dfa& d) {
  std::vector<std::uint64_t> t(std::size_t{d.modes()} * 4, 0);
  for (std::uint32_t m = 0; m < d.modes(); ++m) {
    const std::uint16_t s0 = d.start(m);
    if (d.relay(s0) != kNoRelay) continue;
    for (unsigned b = 0; b < 256; ++b) {
      const std::uint16_t s1 = d.next(s0, d.cls(b));
      if (s1 == kDeadState || d.accept(s1) == 0 || d.bounded(s1) ||
          d.relay(s1) != kNoRelay)
        continue;
      if (const TermInfo& term = d.term(d.accept(s1) - 1u);
          !term.trivia || term.recorded || term.refuses)
        continue;
      bool loops = true;
      for (unsigned c = 0; c < d.classes() && loops; ++c) {
        const std::uint16_t n = d.next(s1, c);
        if (n == kDeadState) continue;
        if (n != s1 || d.next(s0, c) != s1) loops = false;
      }
      if (loops)
        t[std::size_t{m} * 4 + (b >> 6)] |= std::uint64_t{1} << (b & 63u);
    }
  }
  return t;
}

// Whether terminal `t` is an unguarded one-byte literal, which a mode may read
// off its byte alone...
template <class Dfa>
bool lex_byte_spelled(const Dfa& d, unsigned t) {
  return d.text(t).size() == 1 && d.term(t).spelled && !d.folded(t) &&
         !d.term(t).refuses;
}

// ...and whether mode `m` does: the literal has no continuation, and no mark
// echoed from the token's start that a walk would read further.
template <class Dfa>
bool lex_byte_literal(const Dfa& d, unsigned m, unsigned t) {
  if (!lex_byte_spelled(d, t) || d.relay(d.start(m)) != kNoRelay) return false;
  const std::uint16_t st =
      d.next(d.start(m), d.cls(static_cast<unsigned char>(d.text(t)[0])));
  if (st == kDeadState || d.accept(st) != t + 1u || d.relay(st) != kNoRelay ||
      d.bounded(st))
    return false;
  if constexpr (requires { d.ends(st); })
    if (d.ends(st)) return false;
  for (unsigned c = 0; c < d.classes(); ++c)
    if (d.next(st, c) != kDeadState) return false;
  return true;
}

}  // namespace cursiv
