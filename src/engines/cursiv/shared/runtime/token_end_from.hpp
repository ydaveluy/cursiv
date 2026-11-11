// Where the tokens a condition reads end, counted on the tape.
#pragma once

#include <cassert>

#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/shared/table/CondReach.hpp"

namespace cursiv::engine {

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

// How far the phrase of a condition asked at a caret reads: to the end of the
// `tokens` tokens from `resume` on, or of the text where they are unbounded or
// fewer follow.
inline TextOffset phrase_end(const Cst& cst, TextOffset resume,
                             unsigned tokens) {
  const auto end = static_cast<TextOffset>(cst.text().size());
  if (tokens >= kUnboundedTokens) return end;
  return token_end_from(cst, cst.leaf_after(resume), tokens, end);
}

}  // namespace cursiv::engine
