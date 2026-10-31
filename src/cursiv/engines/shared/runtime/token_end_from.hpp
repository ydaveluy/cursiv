// Where the tokens a condition reads end, counted on the tape.
#pragma once

#include <cassert>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

// The end of the `n`-th token from node `first` on in the tape, `none` where
// fewer follow: the leaves of a postorder tape stand in the order of the text.
// `n` is at least 1: the count is taken down to zero and would wrap from 0.
inline TextOffset token_end_from(const Cst& cst, NodeId first, unsigned n,
                                 TextOffset none) {
  assert(n != 0);
  for (NodeId id = first; id < cst.size(); ++id) {
    const CstNode& node = cst[id];
    if (node.subtreeSize == 1 && !node.hidden && node.begin != node.end &&
        --n == 0)
      return node.end;
  }
  return none;
}

// The same, the end of the text where fewer follow.
inline TextOffset token_end_from(const Cst& cst, NodeId first, unsigned n) {
  return token_end_from(cst, first, n,
                        static_cast<TextOffset>(cst.text().size()));
}

}  // namespace cursiv
