// The records a token stands in, and the rules whose frames their stamps say
// the descent had open there.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

// The child of `parent` that holds node `id`.
inline NodeId ll_child_holding(const Cst& cst, NodeId parent, NodeId id) {
  const NodeId first = cst.subtree_begin(parent);
  for (NodeId cursor = parent; cursor > first;) {
    const NodeId begin = cst.subtree_begin(cursor - 1);
    if (begin <= id) return cursor - 1;
    cursor = begin;
  }
  return kNoNode;
}

// The records that hold node `id`, the entry's first.
inline void ll_ancestors(const Cst& cst, NodeId id, std::vector<NodeId>& out) {
  out.clear();
  for (NodeId at = cst.entry_root();
       at != kNoNode && at != id && cst.contains(at, id);
       at = ll_child_holding(cst, at, id))
    out.push_back(at);
}

// ...for a node that stands near one whose records are known: only the
// records below the last they share are looked for.
inline void ll_ancestors_near(const Cst& cst, std::span<const NodeId> known,
                              NodeId id, std::vector<NodeId>& out) {
  std::size_t shared = 0;
  while (shared < known.size() && cst.contains(known[shared], id)) ++shared;
  if (shared == 0) return ll_ancestors(cst, id, out);
  out.assign(known.begin(), known.begin() + shared);
  for (NodeId at = ll_child_holding(cst, out.back(), id);
       at != kNoNode && at != id; at = ll_child_holding(cst, at, id))
    out.push_back(at);
}

// The rules the stamped records among them name, the innermost first. False
// where a repair touched one: its content is no longer the grammar's.
inline bool ll_stamped_rules(const Cst& cst, StampLayout stamp,
                             std::span<const NodeId> records,
                             std::vector<std::uint16_t>& rules) {
  rules.clear();
  for (std::size_t i = records.size(); i-- > 0;) {
    const CstNode& n = cst[records[i]];
    const auto rule = static_cast<std::uint16_t>(stamp.state(n) >> 1);
    if (rule == 0) continue;
    if (n.recovered) return false;
    rules.push_back(rule);
  }
  return true;
}

}  // namespace cursiv
