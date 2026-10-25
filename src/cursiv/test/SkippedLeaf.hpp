// Text a repair gave up: a recovered leaf no read reached, stamped in the
// layout of the table that wrote the tree, which only its grammar knows.
#pragma once

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/engines/shared/runtime/StampedGrammar.hpp"

namespace cursiv::test {

inline bool skipped_leaf(const cursiv::Grammar& grammar,
                         const cursiv::CstNode& n) {
  const auto* stamped = dynamic_cast<const cursiv::StampedGrammar*>(&grammar);
  return n.recovered && stamped != nullptr && stamped->stamp().state(n) == 0;
}

}  // namespace cursiv::test
