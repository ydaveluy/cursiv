// The close an acceptance owes when a reduction left a tree unadopted.
#pragma once

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/engines/shared/table/StampLayout.hpp"

namespace cursiv {

// An unwind closes its composite unnamed, for the reduction above it to name.
// An acceptance with no such reduction above leaves that composite standing
// BESIDE the root instead of under it, and the document keeps a subtree the
// tree does not describe. Leaves outside a root are ordinary -- trivia, a
// repair's discards, a token the entry never spanned -- unless one names a
// row: the tree owes a node for it, and nothing is left above to take it.
// Returns the entry: the root, or the close that roofs it.
template <StampLayout L>
NodeId lr_roof_forest(CstRun& stream, NodeId root, ElementId entryRow) {
  const NodeCount records = stream.records();
  const NodeId lo = root + 1 - stream.at(root).subtreeSize;
  // A whole document usually sits under the root: the two ends are the walk.
  const auto orphan = [&](NodeId from, NodeId to) {
    for (NodeId i = from; i < to; ++i) {
      const CstNode& n = stream.at(i);
      if (n.hidden) continue;
      if (n.subtreeSize > 1 || L.row(n) != kNoElement) return true;
    }
    return false;
  };
  const bool after = orphan(root + 1, records);
  if (!orphan(0, lo) && !after) return root;
  // A root that names the document already is the one node over it: a second
  // would leave the first one's text out of the tree. Its own record takes
  // what stands before it; one it shares with other nodes is roofed bare.
  CstNode& r = stream.at(root);
  if (!after && L.row(r) == entryRow) {
    r.begin = stream.at(0).begin;
    r.subtreeSize = root + 1;
    r.recovered = true;
    return root;
  }
  const ElementId row = L.row(r) != kNoElement ? kNoElement : entryRow;
  // The extent counts the closing record too -- it stands at `records`.
  stream.repaired_close(stream.at(0).begin, stream.at(records - 1).end,
                        records + 1, L.make(row, 0));
  return records;
}

}  // namespace cursiv
