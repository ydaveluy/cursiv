// The stacks that fork. Readings reaching one state with different stacks are
// one reading over a cell: the stacks it may stand on, each with the words of
// the reading that had it. A cell is split into its stacks only where the
// reading returns past it, and every word it read since is then laid under
// each stack's own words.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "cursiv/engines/ll/runtime/LlHistory.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"

namespace cursiv {

class LlStackSet {
 public:
  struct Member {
    std::uint32_t stack;
    LlHistory::Node arm, lex;
    std::uint16_t root;
  };

  static constexpr std::uint32_t kCellBit = 0x80000000u;

  static bool is_cell(std::uint32_t stack) {
    return stack != kLlCtxNone && (stack & kCellBit) != 0;
  }

  void clear() {
    _used = 0;
    _shapes.clear();
  }

  // A cell of two stacks; `first` leads, and is the reading whose words the
  // cell's readings go on extending.
  std::uint32_t fork(const Member& first, const Member& second) {
    if (_used == _cells.size()) _cells.emplace_back();
    Cell& c = _cells[_used];
    c.members.clear();
    c.members.push_back(first);
    c.members.push_back(second);
    c.byCall = false;
    return kCellBit | static_cast<std::uint32_t>(_used++);
  }

  // A cell of one stack, which every later caller of its rule joins.
  std::uint32_t open(const Member& only) {
    if (_used == _cells.size()) _cells.emplace_back();
    Cell& c = _cells[_used];
    c.members.clear();
    c.members.push_back(only);
    c.byCall = true;
    return kCellBit | static_cast<std::uint32_t>(_used++);
  }

  // Whether the callers of a rule opened the cell: no other reading joins it.
  bool by_call(std::uint32_t cell) const { return _cells[index(cell)].byCall; }

  bool holds(std::uint32_t cell, std::uint32_t stack) const {
    return std::ranges::any_of(
        _cells[index(cell)].members,
        [&](const Member& m) { return m.stack == stack; });
  }

  std::size_t size() const { return _used; }
  static std::size_t index(std::uint32_t cell) { return cell & ~kCellBit; }

  void add(std::uint32_t cell, const Member& member) {
    _cells[index(cell)].members.push_back(member);
  }

  // A cell is its shape, whatever order its stacks joined it in: two cells
  // over the same stacks are one.
  std::uint64_t canon(std::uint32_t stack) const {
    return is_cell(stack)
               ? _cells[index(stack)].shape | (std::uint64_t{1} << 63)
               : stack;
  }
  void seal(std::uint32_t cell) {
    Cell& c = _cells[index(cell)];
    _sorted.clear();
    for (const Member& m : c.members) _sorted.push_back(canon(m.stack));
    std::sort(_sorted.begin(), _sorted.end());
    c.shape = _shapes.try_emplace(_sorted, _shapes.size()).first->second;
  }

  // The leading member first: its words are those the cell's readings go on
  // extending, and the ones the others are laid under.
  const std::vector<Member>& members(std::uint32_t cell) const {
    return _cells[index(cell)].members;
  }
  std::vector<Member>& members(std::uint32_t cell) {
    return _cells[index(cell)].members;
  }

 private:
  struct Cell {
    std::vector<Member> members;
    std::uint64_t shape = 0;
    bool byCall = false;
  };

  std::vector<Cell> _cells;
  std::vector<std::uint64_t> _sorted;
  std::map<std::vector<std::uint64_t>, std::uint64_t> _shapes;
  std::size_t _used = 0;
};

}  // namespace cursiv
