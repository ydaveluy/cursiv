// What a lexer reads off a token's first byte without its walk, for every
// engine: the runs of dropped trivia it crosses blind, and the one-byte literals
// it takes whole. Also the part of the automaton's view both engines share.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/shared/table/LexRelay.hpp"
#include "cursiv/shared/table/TermInfo.hpp"
#include "cursiv/shared/table/Token.hpp"

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

// What a mode crosses blind, four words per mode, whatever its grammar calls
// trivia: nothing here knows a character. `skip`: the bytes a run of dropped
// trivia is made of. `stop`: a byte that, right behind such a run, the run's last
// byte may read on with: the reader steps back onto that last byte and reads on
// from it with the walk.
struct LexSkipRuns {
  std::vector<std::uint64_t> skip{};
  std::vector<std::uint64_t> stop{};
};

// Whether only trivia that nobody keeps can be read from state `from`: nothing
// there says where it began, so the read may begin later.
template <class Dfa>
bool lex_reads_only_dropped(const Dfa& d, std::uint16_t from) {
  std::vector<std::uint16_t> states{from};
  std::unordered_set<std::uint16_t> seen{from};
  for (std::size_t i = 0; i < states.size(); ++i) {
    const std::uint16_t s = states[i];
    if constexpr (requires { d.ends(s); })
      if (d.ends(s)) return false;
    if (d.bounded(s) || d.relay(s) != kNoRelay) return false;
    if (d.accept(s) != 0) {
      const TermInfo& term = d.term(d.accept(s) - 1u);
      if (!term.trivia || term.recorded || term.refuses) return false;
    }
    for (unsigned c = 0; c < d.classes(); ++c) {
      const std::uint16_t n = d.next(s, c);
      if (n != kDeadState && seen.insert(n).second) states.push_back(n);
    }
  }
  return true;
}

// A byte belongs to a run where the start takes it to a state that accepts, and
// from which only dropped trivia reads. Two such bytes in a row leave the walk in
// the state the second reaches from the start, or end its token there, which
// leaves it in that same state: so the state behind a run is the one its last
// byte reaches alone, and what the run reads on with is read from that byte as
// well as from the run's first. A byte that breaks this is out. A byte stops a run
// where the run reads on with it into a state a read from the start would not
// stand in: the reader steps back onto the run's last byte, which is always
// exact. A mode whose start launches a relay crosses nothing: the token it reads
// may begin with the run.
template <class Dfa>
LexSkipRuns lex_skip_runs(const Dfa& d) {
  LexSkipRuns out;
  out.skip.assign(std::size_t{d.modes()} * 4, 0);
  out.stop.assign(std::size_t{d.modes()} * 4, 0);
  const unsigned classes = d.classes();
  // A state answers for every class and mode that reach it.
  std::unordered_map<std::uint16_t, bool> dropped;
  for (std::uint32_t m = 0; m < d.modes(); ++m) {
    const std::uint16_t s0 = d.start(m);
    if (d.relay(s0) != kNoRelay) continue;
    // Whether the run reads on with `e` otherwise than a read from the start.
    const auto apart = [&](unsigned c, unsigned e) {
      const std::uint16_t n = d.next(d.next(s0, c), e);
      return n != kDeadState && n != d.next(s0, e);
    };
    std::vector<std::uint8_t> inRun(classes, 0);
    for (unsigned c = 0; c < classes; ++c) {
      const std::uint16_t s1 = d.next(s0, c);
      if (s1 == kDeadState || d.accept(s1) == 0) continue;
      const auto [known, fresh] = dropped.try_emplace(s1, false);
      if (fresh) known->second = lex_reads_only_dropped(d, s1);
      inRun[c] = known->second ? 1 : 0;
    }
    for (bool again = true; again;) {
      again = false;
      for (unsigned c = 0; c < classes; ++c) {
        if (inRun[c] == 0) continue;
        for (unsigned e = 0; e < classes; ++e) {
          // The byte whose state reads on otherwise is the one that goes.
          if (inRun[e] != 0 && apart(c, e)) {
            inRun[c] = 0;
            again = true;
            break;
          }
        }
      }
    }
    std::vector<std::uint8_t> stops(classes, 0);
    for (unsigned c = 0; c < classes; ++c) {
      if (inRun[c] == 0) continue;
      for (unsigned e = 0; e < classes; ++e)
        if (apart(c, e)) stops[e] = 1;
    }
    for (unsigned b = 0; b < 256; ++b) {
      const unsigned c = d.cls(b);
      const std::uint64_t bit = std::uint64_t{1} << (b & 63u);
      if (inRun[c] != 0) out.skip[std::size_t{m} * 4 + (b >> 6)] |= bit;
      if (stops[c] != 0) out.stop[std::size_t{m} * 4 + (b >> 6)] |= bit;
    }
  }
  return out;
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
