// The stacks that fork. Readings reaching one state with different stacks are
// one reading over a cell: the stacks it may stand on, each with the words of
// the reading that had it. A cell is split into its stacks only where the
// reading returns past it, and every word it read since is then laid under
// each stack's own words.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "cursiv/engines/ll/runtime/LlHistory.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"

namespace cursiv {

class LlStackSet {
 public:
  // A member keeps no root of its own: a cell only joins readings that
  // started on one arm, the root of the reading that stands over it.
  struct Member {
    std::uint32_t stack;
    LlHistory::Node arm, lex;
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
    const std::uint32_t cell = open(first);
    _cells[index(cell)].byCall = false;
    add(cell, second);
    return cell;
  }

  // A cell of one stack, which every later caller of its rule joins.
  std::uint32_t open(const Member& only) {
    if (_used == _cells.size()) _cells.emplace_back();
    Cell& c = _cells[_used];
    c.members.assign(1, only);
    c.byCall = true;
    c.sealed = false;
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
    assert(!is_cell(stack) || _cells[index(stack)].sealed);
    return is_cell(stack)
               ? _cells[index(stack)].shape | (std::uint64_t{1} << 63)
               : stack;
  }
  void seal(std::uint32_t cell) {
    if (_cells[index(cell)].sealed) return;
    // A caller can gain a member opened later in the closure. Seal those
    // dependencies first: creation order is not a topological order.
    _pending.clear();
    _pending.push_back({cell});
    while (!_pending.empty()) {
      auto& at = _pending.back();
      Cell& c = _cells[index(at.cell)];
      if (at.next < c.members.size()) {
        const auto child = c.members[at.next++].stack;
        if (is_cell(child) && !_cells[index(child)].sealed)
          _pending.push_back({child});
        continue;
      }
      _sorted.clear();
      for (const Member& m : c.members) _sorted.push_back(canon(m.stack));
      std::sort(_sorted.begin(), _sorted.end());
      c.shape = _shapes.try_emplace(_sorted, _shapes.size()).first->second;
      c.sealed = true;
      _pending.pop_back();
    }
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
    bool sealed = false;
  };

  struct Pending {
    std::uint32_t cell;
    std::size_t next = 0;
  };

  std::vector<Cell> _cells;
  std::vector<std::uint64_t> _sorted;
  std::vector<Pending> _pending;
  std::map<std::vector<std::uint64_t>, std::uint64_t> _shapes;
  std::size_t _used = 0;
};

}  // namespace cursiv
