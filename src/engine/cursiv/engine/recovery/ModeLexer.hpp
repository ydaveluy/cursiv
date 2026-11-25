// The lexer's automaton as the mode of one state reads it: a step is dead
// where no terminal of the mode ends there or past it.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engine/parser/Machine.hpp"

namespace cursiv::engine {

// What a mode reads where the automaton stands: the terminal, where its
// token ends, and the terminal a keyword read there keeps the spelling from.
struct Ending {
  std::uint16_t terminal = kNoToken;
  std::uint16_t reserved = kNoToken;
  TextOffset end = 0;
  // Whether the lexer read past the end to end the token there: a guard of
  // its own, or a keyword's word test.
  bool bounded = false;
  bool literal = false;
};

struct ModeLexer {
  // The mode of `state`: the terminals it acts on, and the trivia. With
  // `fault`, the mode the text of a fault there is read in.
  ModeLexer(Machine& machine, std::uint32_t state, bool fault = false)
      : m(machine), t(machine.t),
        mode(fault ? t.modeFault[t.stateMode[state]] : t.stateMode[state]),
        win(t.modes[mode].win),
        onward(t.modes[mode].onward),
        follow(fault ? nullptr : t.setOf(t.followOf[state])) {}

  // Whether a terminal of the mode ends in `s` or past it.
  [[nodiscard]] bool lives(std::uint16_t s) const {
    return s != 0 && (accepts(s) || ((onward[s >> 3] >> (s & 7)) & 1) != 0);
  }
  // Whether mode `other` reads in `s` as this one does.
  [[nodiscard]] bool alike(std::uint16_t other, std::uint16_t s) const {
    const LexMode& o = t.modes[other];
    const std::uint16_t accept = t.lexStates[s].accept;
    return win.at(0, accept) == o.win.at(0, accept) &&
           (((onward[s >> 3] ^ o.onward[s >> 3]) >> (s & 7)) & 1) == 0;
  }
  // Whether the mode reads `terminal`.
  [[nodiscard]] bool reads(std::uint16_t terminal) const {
    return follow == nullptr || t.terminalTrivia[terminal] != Trivia::none ||
           ((follow[terminal / 64] >> (terminal % 64)) & 1) != 0;
  }
  // The terminal of the mode that `s` leads to, the one the lexer ranks
  // first: what a token's body waits for. `kNoToken` for none.
  [[nodiscard]] std::uint16_t leads_to(std::uint16_t s) const {
    const std::uint64_t* reach = t.setOf(t.lexStates[s].reach);
    std::uint16_t first = kNoToken;
    for (std::uint32_t w = 0; w < t.followWords; ++w)
      for (std::uint64_t bits = reach[w]; bits != 0; bits &= bits - 1) {
        const auto terminal = static_cast<std::uint16_t>(w * 64 + std::countr_zero(bits));
        if (reads(terminal) &&
            (first == kNoToken || t.terminalRank[terminal] < t.terminalRank[first]))
          first = terminal;
      }
    return first;
  }
  // Whether a terminal of `set` ends in `s` or past it.
  [[nodiscard]] bool reaches(std::uint16_t s, const std::vector<std::uint64_t>& set) const {
    const std::uint64_t* reach = t.setOf(t.lexStates[s].reach);
    for (std::uint32_t w = 0; w < t.followWords; ++w)
      if ((reach[w] & set[w]) != 0) return true;
    return false;
  }
  // The state byte `b` leads to from `s`, none where the mode reads nothing
  // through it.
  [[nodiscard]] std::uint16_t step(std::uint16_t s, unsigned char b) const {
    const std::uint16_t to = t.transition(s, t.byteClass[b]);
    return lives(to) ? to : std::uint16_t{0};
  }
  // Whether a terminal of the mode ends in `s`.
  [[nodiscard]] bool accepts(std::uint16_t s) const { return win.at(0, t.lexStates[s].accept) != kWinNone; }
  // What the mode reads with the automaton in `s` at `p`.
  [[nodiscard]] Ending ending(std::uint16_t s, TextOffset p, LexWork& work) const {
    // The first bytes of a terminal the matcher reads end nothing.
    LexResolution r = m.reading(win.at(0, t.lexStates[s].accept), m.src + p, &work);
    while (r.test == LexTest::match) r = m.reading(r.otherwise, m.src + p, &work);
    if (work.cut || r.token == kNoToken) return {};
    return {r.token, r.test == LexTest::word && r.otherwise < kWinNone ? r.otherwise : kNoToken,
            p - r.back, r.back != 0 || r.test != LexTest::none, r.literal};
  }

  Machine& m;
  const Tables& t;
  std::uint16_t mode;
  // How the mode reads each set of terminals ending together.
  LexBlocks win;
  // Per state, whether a terminal of the mode ends past it.
  const std::uint8_t* onward;
  // The terminals the state acts on; none for a fault mode.
  const std::uint64_t* follow;
};

}  // namespace cursiv::engine
