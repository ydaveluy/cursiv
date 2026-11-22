// Text a repair skipped: a recovered leaf read as nothing of the grammar.
#pragma once

#include "cursiv/syntax-tree/CstNode.hpp"

namespace cursiv::test::detail {

inline bool skipped_leaf(const cursiv::CstNode& n) {
  return n.isRecovered() && n.element() == cursiv::kSkippedElement;
}

}  // namespace cursiv::test::detail
