// What a state still needs before a construct it is in is whole: the
// terminals a repair may have to insert there.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/parser/Bits.hpp"
#include "cursiv/engine/parser/Lr1.hpp"
#include "cursiv/engine/parser/Tables.hpp"

namespace cursiv::engine::gen {

struct Closers {
  // Per state of the automaton, the first terminals of the shortest
  // completions of its kernel's items, a list's continuation aside.
  std::vector<Bits> rows;
  // Per state its kernel, and per nonterminal its left corners, as the fewest
  // tokens that complete a stack read them (see `Tables::kernels`).
  std::vector<std::vector<std::uint64_t>> kernels;
  std::vector<std::uint32_t> cornerAt;
  std::vector<std::uint32_t> corners;
  // Per nonterminal, whether it may read nothing; per production, whether it
  // goes on from a left operand of its own recursion.
  std::vector<bool> nullable;
  std::vector<bool> turns;
};

Closers closers(const Cfg& cfg, const LrAutomaton& automaton);

}  // namespace cursiv::engine::gen
