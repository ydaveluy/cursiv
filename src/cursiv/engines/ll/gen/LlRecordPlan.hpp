// A position's record: whether every reading hands one back, whether it opens
// a region, and the chains it can carry. Ownership precedes redistribution;
// chain propagation follows it and retains the order in which joins are made.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
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

  // Only a structural singleton can absorb a group's chain before fusion.
  std::uint16_t single_position(std::uint16_t id) const {
    const LlNode& n = _cfg.nodes[id];
    if (n.kind == LlShape::rep)
      return n.min == 1 && n.max == 1 ? n.sym : kNoRow;
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
    _hands.assign(_cfg.nrules, true);
    _opens.assign(_cfg.nrules, false);
    // A record is guaranteed only when every reading supplies it; opening a
    // region needs one finite reading that does so. The two never read each
    // other, so one walk settles both.
    for (bool moved = true; moved;) {
      moved = false;
      for (std::uint16_t r = 1; r < _cfg.nrules; ++r) {
        const Flags flags = rule_flags(r);
        if (_hands[r] && !flags.hands) _hands[r] = false, moved = true;
        if (!_opens[r] && flags.opens) _opens[r] = true, moved = true;
      }
    }
  }

  bool hands(std::uint16_t node) const { return node_flags(node).hands; }
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
    _nodeRows.assign(_cfg.nnodes, {});
    _ruleRows.assign(_cfg.nrules, {});
    for (bool moved = true; moved;) {
      moved = false;
      for (auto& row : _nodeRows) row.clear();
      for (std::uint32_t id = 0; id < _cfg.nnodes; ++id) {
        const LlNode& n = _cfg.nodes[id];
        auto& out = _nodeRows[id];
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
      }
      for (std::uint16_t r = 1; r < _cfg.nrules; ++r) {
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
        moved = moved || out.size() != before;
      }
    }
  }

 private:
  struct Flags {
    bool hands = false, opens = false;
  };
  enum class Source { last, sole };

  static bool group(const LlNode& n) {
    return n.kind == LlShape::seq || n.kind == LlShape::alt ||
           n.kind == LlShape::rep;
  }

  Flags rule_flags(std::uint16_t rule) const {
    const LlRuleInfo& u = _cfg.rules[rule];
    if (u.climb != kNoRow) {
      const auto primary = _cfg.climbs[u.climb].primary;
      return {primary == kNoRow || _hands[primary], true};
    }
    return u.body == kNoRow ? Flags{} : node_flags(u.body);
  }

  Flags node_flags(std::uint16_t id) const {
    struct Frame {
      std::uint16_t id, only = kNoRow;
      Flags flags;
      std::span<const std::uint16_t> children;
      std::size_t next = 0;
      bool all = true;
    };
    const auto frame_for = [&](std::uint16_t current) {
      const LlNode& n = _cfg.nodes[current];
      Frame frame{.id = current,
                  .flags = {n.row != kNoRow, opens_chain(n.row)},
                  .children = {}};
      if (frame.flags.hands && frame.flags.opens) return frame;
      if (n.kind == LlShape::term || n.kind == LlShape::implied) {
        frame.flags.hands = true;
      } else if (n.kind == LlShape::call) {
        frame.flags.hands = frame.flags.hands || _hands[n.sym];
        frame.flags.opens = frame.flags.opens || _opens[n.sym];
      } else if (group(n)) {
        frame.only = single_position(current);
        frame.children = _cfg.children(n);
      }
      return frame;
    };
    // Counted bounds can nest thousands of groups. Keep the walk on the heap
    // rather than making one native call for each level of the lowered graph.
    std::vector<Frame> stack{frame_for(id)};
    for (;;) {
      auto& frame = stack.back();
      if (frame.next < frame.children.size()) {
        const auto child = frame.children[frame.next++];
        stack.push_back(frame_for(child));
        continue;
      }
      Flags out = frame.flags;
      if (_cfg.nodes[frame.id].kind == LlShape::alt)
        out.hands = out.hands || frame.all;
      const auto child = frame.id;
      stack.pop_back();
      if (stack.empty()) return out;
      auto& parent = stack.back();
      parent.all = parent.all && out.hands;
      if (child == parent.only)
        parent.flags.hands = parent.flags.hands || out.hands;
      parent.flags.opens = parent.flags.opens || out.opens;
    }
  }

  bool takes_none(std::uint16_t id) const {
    return _nullable[id] != 0 || _cfg.nodes[id].kind == LlShape::endMark;
  }

  // Last keeps a sequence's nullable suffix; sole keeps every other position
  // nullable too. A transparent nested group is reopened only for sole.
  void sources(std::uint16_t id, Source source,
               std::vector<std::uint16_t>& out) const {
    const LlNode& n = _cfg.nodes[id];
    const unsigned count = n.kind == LlShape::rep ? 1u : n.count;
    for (unsigned i = 0; i < count; ++i) {
      const auto k = n.kind == LlShape::rep ? n.sym : _cfg.kids[n.sym + i];
      if (n.kind == LlShape::seq) {
        bool eligible = true;
        for (unsigned j = source == Source::last ? i + 1 : 0;
             j < n.count && eligible; ++j)
          if (j != i) eligible = takes_none(_cfg.kids[n.sym + j]);
        if (!eligible) continue;
      }
      const LlNode& child = _cfg.nodes[k];
      if (source == Source::last || child.owns || !group(child))
        append(out, _nodeRows[k]);
      else
        sources(k, source, out);
    }
  }

  const LlLowCfg& _cfg;
  const ChainPool& _writes;
  const std::vector<std::uint8_t>& _nests;
  std::vector<bool> _hands, _opens;
  std::span<const std::uint8_t> _nullable;
  std::vector<std::vector<std::uint16_t>> _nodeRows, _ruleRows;
};

}  // namespace cursiv
