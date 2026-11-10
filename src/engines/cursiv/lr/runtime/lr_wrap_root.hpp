// The close an acceptance needs when a reduction left a tree unadopted.
#pragma once

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/shared/table/StampLayout.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"

namespace cursiv::engine {

// An unwind closes its composite unnamed, for the reduction above to name.
// Where acceptance has no such reduction, the composite, or a leaf outside the
// root that names a row, is wrapped with the root. Returns the root or its
// wrapper.
template <StampLayout L>
NodeId lr_wrap_root(CstRun& stream, NodeId root, ElementId entryRow) {
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
  if (!orphan(0, lo) && !after) {
    // What a repair dropped before the first token or after the last is the
    // document's text: the root takes it, though not the trivia around it.
    NodeId first = 0;
    while (first < lo && stream.at(first).hidden) ++first;
    NodeId last = records;
    while (last > root + 1 && stream.at(last - 1).hidden) --last;
    CstNode r = stream.at(root);
    if (first < lo) r.begin = stream.at(first).begin;
    for (NodeId i = root; i + 1 < last; ++i) stream.at(i) = stream.at(i + 1);
    if (last > root + 1) r.end = stream.at(last - 2).end;
    r.subtreeSize = last - first;
    stream.at(last - 1) = r;
    return last - 1;
  }
  // A root that names the document already is the one node over it: a second
  // would leave the first one's text out of the tree. Its own record takes
  // what stands before it; one it shares with other nodes is wrapped bare.
  CstNode& r = stream.at(root);
  if (!after && L.row(r) == entryRow) {
    r.begin = stream.at(0).begin;
    r.subtreeSize = root + 1;
    r.recovered = true;
    return root;
  }
  const ElementId row = L.row(r) != kNoElement ? kNoElement : entryRow;
  // The extent counts the closing record too: it stands at `records`.
  stream.repaired_close(stream.at(0).begin, stream.at(records - 1).end,
                        records + 1, L.make(row, 0));
  return records;
}

}  // namespace cursiv::engine
