// A position's record: whether every reading hands one back, whether it opens
// a region, and the chains it can carry. Ownership precedes redistribution;
// chain propagation follows it and retains the order in which joins are made.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/gen/LlLowCfg.hpp"
#include "cursiv/engines/ll/table/LlFuse.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"

namespace cursiv {

class LlRecordPlan {
 public:
  LlRecordPlan(const LlLowCfg& cfg, const ChainPool& writes,
               const std::vector<std::uint8_t>& nests)
      : _cfg(cfg), _writes(writes), _nests(nests) {}

  bool opens_chain(std::uint16_t row) const {
    return row != kNoRow && (_writes.chainBuild[row].creates ||
                             (row < _nests.size() && _nests[row] != 0));
  }

  // Only a structural singleton can absorb a group's chain before fusion: a
  // sequence of one position. A repetition is an option or a loop, never a
  // single turn.
  std::uint16_t single_position(std::uint16_t id) const {
    const LlNode& n = _cfg.nodes[id];
    if (n.kind != LlShape::seq) return kNoRow;
    std::uint16_t only = kNoRow;
    for (unsigned i = 0; i < n.count; ++i) {
      const auto k = _cfg.kids[n.sym + i];
      const auto kind = _cfg.nodes[k].kind;
      if (kind == LlShape::empty || kind == LlShape::endMark ||
          kind == LlShape::condition)
        continue;
      if (only != kNoRow) return kNoRow;
      only = k;
    }
    return only;
  }

  void settle_ownership() {
    const auto count = _cfg.nnodes + _cfg.nrules;
    std::vector<std::vector<std::uint32_t>> handsUsers(count),
        opensUsers(count);
    std::vector<bool> hands(count, true), opens(count, false);
    std::vector<std::uint32_t> noHands, doesOpen;
    const auto rule = [&](std::uint16_t r) { return _cfg.nnodes + r; };
    const auto supplies = [&](std::uint32_t from, std::uint32_t to) {
      handsUsers[from].push_back(to);
    };
    const auto opensInto = [&](std::uint32_t from, std::uint32_t to) {
      opensUsers[from].push_back(to);
    };
    for (std::uint32_t id = 0; id < _cfg.nnodes; ++id) {
      const LlNode& n = _cfg.nodes[id];
      if (n.row == kNoRow && n.kind != LlShape::term &&
          n.kind != LlShape::implied) {
        if (n.kind == LlShape::call) {
          supplies(rule(n.sym), id);
        } else if (n.kind == LlShape::alt) {
          for (const auto child : _cfg.children(n)) supplies(child, id);
        } else if (const auto only = single_position(id); only != kNoRow) {
          supplies(only, id);
        } else {
          hands[id] = false;
          noHands.push_back(id);
        }
      }
      if (opens_chain(n.row)) {
        opens[id] = true;
        doesOpen.push_back(id);
      } else if (n.kind == LlShape::call) {
        opensInto(rule(n.sym), id);
      } else if (group(n)) {
        for (const auto child : _cfg.children(n)) opensInto(child, id);
      }
    }
    for (std::uint16_t r = 1; r < _cfg.nrules; ++r) {
      const LlRuleInfo& u = _cfg.rules[r];
      const auto id = rule(r);
      if (u.climb != kNoRow) {
        const auto primary = _cfg.climbs[u.climb].primary;
        if (primary != kNoRow) supplies(rule(primary), id);
        opens[id] = true;
        doesOpen.push_back(id);
      } else if (u.body != kNoRow) {
        supplies(u.body, id);
        opensInto(u.body, id);
      } else {
        hands[id] = false;
        noHands.push_back(id);
      }
    }
    // Guarantees start true and lose callers; openings start false and reach
    // callers from a finite opening. Each flag crosses each dependency once.
    const auto propagate = [](auto& values, const auto& users, auto& todo,
                              bool value) {
      for (std::size_t i = 0; i < todo.size(); ++i)
        for (const auto user : users[todo[i]])
          if (values[user] != value) {
            values[user] = value;
            todo.push_back(user);
          }
    };
    propagate(hands, handsUsers, noHands, false);
    propagate(opens, opensUsers, doesOpen, true);
    _nodeHands.assign(hands.begin(), hands.begin() + _cfg.nnodes);
    _hands.assign(hands.begin() + _cfg.nnodes, hands.end());
    _opens.assign(opens.begin() + _cfg.nnodes, opens.end());
  }

  // Whether every reading of a position hands back a record. Moving a chain
  // down to a group's only position keeps the guarantee of both.
  bool hands(std::uint16_t node) const { return _nodeHands[node]; }
  bool rule_hands(std::uint16_t rule) const { return _hands[rule]; }
  bool rule_opens(std::uint16_t rule) const { return _opens[rule]; }
  const std::vector<std::uint16_t>& rule_rows(std::uint16_t rule) const {
    return _ruleRows[rule];
  }

  static void add(std::vector<std::uint16_t>& out, std::uint16_t row) {
    for (const auto x : out)
      if (x == row) return;
    out.push_back(row);
  }
  static void append(std::vector<std::uint16_t>& out,
                     const std::vector<std::uint16_t>& rows) {
    for (const auto row : rows) add(out, row);
  }

  template <class Join, class Sink>
  void own_joins(std::uint16_t id, Join&& join, Sink&& sink) const {
    assert(!_nodeRows.empty());
    const LlNode& n = _cfg.nodes[id];
    if (_cfg.nodeBuild[id].foldStep) return;
    std::vector<std::uint16_t> inner;
    if (n.kind == LlShape::call)
      append(inner, _ruleRows[n.sym]);
    else if (group(n))
      sources(id, Source::sole, inner);
    else
      return;
    const bool nests =
        n.row != kNoRow && n.row < _nests.size() && _nests[n.row] != 0;
    for (const auto row : inner) {
      if (nests && row != kNoRow && _writes.chainBuild[row].makes) continue;
      sink(LlFuse{row, join(row, n.row)});
    }
  }

  template <class Join>
  void settle_rows(std::span<const std::uint8_t> nullable, Join&& join) {
    _nullable = nullable;
    _last.assign(_cfg.nnodes, 0);
    _required.assign(_cfg.nnodes, 0);
    for (std::uint32_t id = 0; id < _cfg.nnodes; ++id) {
      const LlNode& n = _cfg.nodes[id];
      if (n.kind != LlShape::seq) continue;
      for (unsigned i = 0; i < n.count; ++i)
        if (!takes_none(_cfg.kids[n.sym + i])) {
          _last[id] = static_cast<std::uint16_t>(i);
          ++_required[id];
        }
    }
    _nodeRows.assign(_cfg.nnodes, {});
    _ruleRows.assign(_cfg.nrules, {});
    const auto count = _cfg.nnodes + _cfg.nrules;
    std::vector<std::vector<std::uint32_t>> users(count);
    const auto rule = [&](std::uint16_t r) { return _cfg.nnodes + r; };
    for (std::uint32_t id = 0; id < _cfg.nnodes; ++id) {
      const LlNode& n = _cfg.nodes[id];
      if (n.owns && _cfg.nodeBuild[id].foldStep) continue;
      if (n.kind == LlShape::call) {
        users[rule(n.sym)].push_back(id);
      } else if (group(n)) {
        each_source(static_cast<std::uint16_t>(id),
                    n.owns ? Source::sole : Source::last,
                    [&](std::uint16_t child) { users[child].push_back(id); });
      }
    }
    for (std::uint16_t r = 1; r < _cfg.nrules; ++r) {
      const LlRuleInfo& u = _cfg.rules[r];
      if (u.climb != kNoRow) {
        const LlClimb& cl = _cfg.climbs[u.climb];
        users[cl.primary != kNoRow ? rule(cl.primary) : cl.primaryNode]
            .push_back(rule(r));
      } else if (u.body != kNoRow) {
        users[u.body].push_back(rule(r));
      }
    }
    // Keep the former scan's rounds and position order: rule rows retain the
    // order in which joins first reach them. Only changed dependencies ask a
    // position to run again; aliases no longer rescan unrelated rules.
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>,
                        std::greater<>>
        todo;
    std::vector<bool> queued(count, true);
    for (std::uint32_t id = 0; id < count; ++id)
      if (id != _cfg.nnodes) todo.push(id);  // rule zero has no body
    while (!todo.empty()) {
      const auto step = todo.top();
      todo.pop();
      const auto id = static_cast<std::uint32_t>(step);
      queued[id] = false;
      bool moved = false;
      if (id < _cfg.nnodes) {
        const LlNode& n = _cfg.nodes[id];
        std::vector<std::uint16_t> out;
        if (n.owns) {
          add(out, n.row);
          own_joins(static_cast<std::uint16_t>(id), join,
                    [&](const LlFuse& j) { add(out, j.to); });
        } else if (n.kind == LlShape::term || n.kind == LlShape::implied) {
          add(out, n.row);
        } else if (n.kind == LlShape::call) {
          for (const auto row : _ruleRows[n.sym])
            add(out, n.row == kNoRow ? row : join(row, n.row));
        } else if (group(n)) {
          sources(static_cast<std::uint16_t>(id), Source::last, out);
        }
        moved = out != _nodeRows[id];
        if (moved) _nodeRows[id] = std::move(out);
      } else {
        const auto r = static_cast<std::uint16_t>(id - _cfg.nnodes);
        const LlRuleInfo& u = _cfg.rules[r];
        auto& out = _ruleRows[r];
        const auto before = out.size();
        if (u.climb != kNoRow) {
          const LlClimb& cl = _cfg.climbs[u.climb];
          add(out, cl.node);
          append(out, cl.primary != kNoRow ? _ruleRows[cl.primary]
                                           : _nodeRows[cl.primaryNode]);
        } else if (u.body != kNoRow) {
          append(out, _nodeRows[u.body]);
        }
        moved = out.size() != before;
      }
      if (!moved) continue;
      for (const auto user : users[id]) {
        if (queued[user]) continue;
        queued[user] = true;
        const auto round = (step >> 32) + (user <= id);
        todo.push((round << 32) | user);
      }
    }
  }

 private:
  enum class Source { last, sole };

  static bool group(const LlNode& n) {
    return n.kind == LlShape::seq || n.kind == LlShape::alt ||
           n.kind == LlShape::rep;
  }

  // Whether a reading of the position may lay no record: it matches nothing,
  // is the end, or is a choice with such an arm.
  bool takes_none(std::uint16_t id) const {
    const auto none = [&](std::uint16_t k) {
      return _nullable[k] != 0 || _cfg.nodes[k].kind == LlShape::endMark;
    };
    if (none(id)) return true;
    const LlNode& n = _cfg.nodes[id];
    return n.kind == LlShape::alt &&
           std::ranges::any_of(_cfg.children(n), none);
  }

  // Last keeps a sequence's nullable suffix; sole keeps every other position
  // nullable too. A transparent nested group is reopened only for sole.
  template <class Visit>
  void each_source(std::uint16_t id, Source source, Visit&& visit) const {
    if (source == Source::last) {
      const LlNode& n = _cfg.nodes[id];
      const auto children = _cfg.children(n);
      const unsigned first = n.kind == LlShape::seq ? _last[id] : 0u;
      for (unsigned i = first; i < children.size(); ++i) visit(children[i]);
      return;
    }
    struct Frame {
      std::uint16_t id;
      unsigned next = 0;
    };
    std::vector<Frame> todo{{id}};
    while (!todo.empty()) {
      Frame& f = todo.back();
      const LlNode& n = _cfg.nodes[f.id];
      const auto children = _cfg.children(n);
      if (f.next == children.size()) {
        todo.pop_back();
        continue;
      }
      const auto k = children[f.next++];
      if (n.kind == LlShape::seq && _required[f.id] != 0 &&
          (_required[f.id] != 1 || takes_none(k)))
        continue;
      const LlNode& child = _cfg.nodes[k];
      if (child.owns || !group(child))
        visit(k);
      else
        todo.push_back({k});
    }
  }

  void sources(std::uint16_t id, Source source,
               std::vector<std::uint16_t>& out) const {
    each_source(id, source,
                [&](std::uint16_t child) { append(out, _nodeRows[child]); });
  }

  const LlLowCfg& _cfg;
  const ChainPool& _writes;
  const std::vector<std::uint8_t>& _nests;
  std::vector<bool> _nodeHands, _hands, _opens;
  std::span<const std::uint8_t> _nullable;
  std::vector<std::uint16_t> _last, _required;
  std::vector<std::vector<std::uint16_t>> _nodeRows, _ruleRows;
};

}  // namespace cursiv
