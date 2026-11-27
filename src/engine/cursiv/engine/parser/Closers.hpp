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
  // Per state its kernel, per nonterminal the nonterminals its productions
  // begin with, each with the fewest tokens past it, and the fewest tokens it
  // reads: what the fewest tokens that complete a stack are read from (see
  // `Tables::kernels`).
  std::vector<std::vector<std::uint64_t>> kernels;
  std::vector<std::vector<std::pair<std::uint32_t, std::uint64_t>>> begins;
  std::vector<std::uint16_t> yields;
  // Per nonterminal, whether it may read nothing; per production, whether it
  // goes on from a left operand of its own recursion.
  std::vector<bool> nullable;
  std::vector<bool> turns;

  // Of each nonterminal `asked` names, the nonterminals its derivations begin
  // with that `completed` names, ascending, as `nonterminal | tokens << 16`
  // into `out`; per nonterminal, where its own begin into `cornerAt`.
  void corners(const std::vector<bool>& asked, const std::vector<bool>& completed,
               std::vector<std::uint32_t>& cornerAt, std::vector<std::uint32_t>& out) const;
};

Closers closers(const Cfg& cfg, const LrAutomaton& automaton);

}  // namespace cursiv::engine::gen
