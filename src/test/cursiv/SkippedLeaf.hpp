// Text a repair skipped: a recovered leaf no read reached. Only the engine's
// `StampedGrammar` can tell it.
#pragma once

#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/shared/runtime/StampedGrammar.hpp"

namespace cursiv::test::detail {

inline bool skipped_leaf(const cursiv::Grammar& grammar,
                         const cursiv::CstNode& n) {
  const auto* stamped =
      dynamic_cast<const cursiv::engine::StampedGrammar*>(&grammar);
  return n.recovered && stamped != nullptr && stamped->stamp().state(n) == 0;
}

}  // namespace cursiv::test::detail
