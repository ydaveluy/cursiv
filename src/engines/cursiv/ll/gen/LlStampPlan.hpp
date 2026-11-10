// Which groups create a record that names the rule it was created in, and how
// many such records stand open around each position: the two halves of what a
// caret reads off the tree to learn the stack the descent held.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/ll/gen/LlBuiltProgram.hpp"
#include "cursiv/ll/table/LlNode.hpp"

namespace cursiv::engine {

class LlStampPlan {
 public:
  explicit LlStampPlan(const LlBuiltProgram& a)
      : _a(a),
        _stamps(a.cfg.nnodes, 0),
        _covers(a.cfg.nnodes, 0),
        _heads(a.cfg.nnodes, 0),
        _reaches(a.cfg.nnodes, 0) {
    for (std::uint32_t id = 0; id < a.cfg.nnodes; ++id)
      _heads[id] = heads_over(static_cast<std::uint16_t>(id));
    for (std::uint32_t id = 0; id < a.cfg.nnodes; ++id)
      _reaches[id] = reaches_over(static_cast<std::uint16_t>(id));
    // A record's stamp holds the rule shifted past the turn bit, in the
    // fourteen bits a record keeps for its state.
    if (a.cfg.nrules > kRules || a.cfg.sections()) return;
    for (std::uint32_t id = 0; id < a.cfg.nnodes; ++id)
      _stamps[id] = writes_named(static_cast<std::uint16_t>(id));
    // A group follows its members and has one parent: walked down, a position
    // reads the count its parent left.
    for (std::size_t id = a.cfg.nnodes; id-- > 0;) {
      const unsigned open = _covers[id] + (_stamps[id] ? 1u : 0u);
      if (open > 0xFFu) {
        _stamps.assign(a.cfg.nnodes, 0);
        _covers.assign(a.cfg.nnodes, 0);
        return;
      }
      _covers[id] = static_cast<std::uint8_t>(open);
      for (const auto x : a.cfg.children(a.cfg.nodes[id])) {
        assert(x < id);
        _covers[x] = _covers[id];
      }
    }
  }

  // A group whose record is a nest turn.
  bool turns(std::uint16_t id) const {
    const LlNode& n = node(id);
    return n.owns && !_a.cfg.nodeBuild[id].foldStep &&
           n.row < _a.nestChains.size() && _a.nestChains[n.row] != 0;
  }

  // A position whose first reading is a nest turn.
  bool head_turns(std::uint16_t id) const { return _heads[id] != 0; }

  // Whether a position of this sequence past its head turns: its turns reach
  // back over what the sequence read before it.
  bool reaches_back(std::uint16_t id) const { return _reaches[id] != 0; }

  // The record of this group is created, whole and in place, by the rule whose
  // body holds it: no turn reaches back over it and no reused subtree replaces
  // it.
  bool stamps(std::uint16_t id) const { return _stamps[id] != 0; }

  // How many groups that stamp enclose a position of the same rule, itself
  // included.
  std::uint8_t covers(std::size_t node) const { return _covers[node]; }

  // The state of a record created by a group of `rule`.
  static constexpr std::uint16_t state(std::uint16_t rule) {
    return static_cast<std::uint16_t>(rule << 1);
  }

 private:
  static constexpr std::uint32_t kRules = 1u << 13;

  const LlNode& node(std::uint16_t id) const { return _a.cfg.nodes[id]; }
  std::uint16_t child_at(const LlNode& n, unsigned i) const {
    return _a.cfg.childIds[n.sym + i];
  }

  // A group's id is above its members', so a child's answer is already there.
  bool heads_over(std::uint16_t id) const {
    if (turns(id)) return true;
    const LlNode& n = node(id);
    // A creation starts another construction context. An adopting close
    // leaves the current node in place, including at a folded rule's head.
    if (n.owns && _a.writes.chainBuild[n.row].creates) return false;
    if (n.kind == LlShape::rep) {
      assert(n.sym < id);
      return _heads[n.sym] != 0;
    }
    if (n.kind == LlShape::seq) {
      for (unsigned i = 0; i < n.count; ++i) {
        const auto child = child_at(n, i);
        assert(child < id);
        if (_heads[child] != 0) return true;
        if (!_a.nullable[child]) return false;
      }
      return false;
    }
    if (n.kind == LlShape::alt)
      for (unsigned i = 0; i < n.count; ++i) {
        assert(child_at(n, i) < id);
        if (_heads[child_at(n, i)] != 0) return true;
      }
    return false;
  }

  bool reaches_over(std::uint16_t id) const {
    const LlNode& n = node(id);
    if (n.kind != LlShape::seq) return false;
    for (unsigned k = 1; k < n.count; ++k)
      if (head_turns(child_at(n, k))) return true;
    return false;
  }

  bool writes_named(std::uint16_t id) const {
    const LlNode& n = node(id);
    return n.owns && n.kind != LlShape::empty && !turns(id) &&
           !_a.cfg.nodeBuild[id].foldStep &&
           _a.ownBase[id + 1] == _a.ownBase[id] && !reaches_back(id);
  }

  const LlBuiltProgram& _a;
  std::vector<std::uint8_t> _stamps;
  std::vector<std::uint8_t> _covers;
  std::vector<std::uint8_t> _heads;
  std::vector<std::uint8_t> _reaches;
};

}  // namespace cursiv::engine
