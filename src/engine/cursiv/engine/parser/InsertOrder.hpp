// The order a repair tries the terminals a state acts on: as a top-down,
// left-to-right reading of the grammar reaches them from the state's items,
// innermost first. A rule that can end hands over to the states it returns
// to.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>

#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/parser/Bits.hpp"
#include "cursiv/engine/parser/Closers.hpp"
#include "cursiv/engine/parser/Lr1.hpp"

namespace cursiv::engine::gen {

class InsertOrder {
 public:
  InsertOrder(const Cfg& cfg, const LrAutomaton& automaton, const Closers& needs);

  // The terminals of `row` that `state` reaches, in the order it reaches
  // them: the walk stops once it met all of those that a rule reads.
  void of(std::uint32_t state, const Bits& row, std::vector<std::uint16_t>& out);

 private:
  using Item = std::pair<std::uint32_t, std::uint32_t>;

  // A list of states, kept once: the states ending a rule return to all its
  // callers, so few lists stand for many states. Once read, it holds its
  // kernel items and the lists they return to.
  struct Listed {
    const std::vector<std::uint32_t>* states;
    std::vector<Item> items{};
    std::vector<std::uint32_t> returns{};
    bool filled = false;
  };
  // Numbers met once a round.
  struct Marks {
    std::vector<std::uint32_t> round;
    std::uint32_t now = 0;
    void next() { ++now; }
    bool first(std::size_t i) {
      if (i >= round.size()) round.resize(i + 1, 0);
      return std::exchange(round[i], now) != now;
    }
  };

  void rank_productions();
  std::uint32_t list_of(std::vector<std::uint32_t> states);
  const Listed& filled(std::uint32_t id);
  void read(const Bits& row, std::size_t want, std::vector<std::uint16_t>& out);
  std::uint32_t returns(std::uint32_t state);

  const Cfg& _cfg;
  const LrAutomaton& _lr;
  const Closers& _needs;
  // Per production, the dot from which what is left may read nothing.
  std::vector<std::uint32_t> _nullFrom;
  std::vector<std::uint64_t> _rank;
  // Per production, its first item number.
  std::vector<std::uint32_t> _base{0};
  std::vector<std::vector<std::uint32_t>> _preds;
  std::vector<std::uint32_t> _returnsOf;
  // The terminals some rule reads.
  Bits _read;
  std::map<std::vector<std::uint32_t>, std::uint32_t> _listIds;
  std::deque<Listed> _lists;
  Marks _seen, _met, _walked, _placed, _under;
  std::vector<std::uint32_t> _level, _next;
  std::vector<Item> _picked, _todo;
};

}  // namespace cursiv::engine::gen
