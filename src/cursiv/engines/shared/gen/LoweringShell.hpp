// What every lowering names of its grammar, and the table it lowers to once.
#pragma once

#include "cursiv/engines/shared/gen/LowerFacts.hpp"

namespace cursiv::detail {

// `Lowering` supplies `lower()`. The closure, the symbol split and the action
// vocabulary are named once: an id is a position in one of these lists, so a
// reader builds its table of thunks from the same list.
template <class Lowering, class G, class Rewrite, class Lowered>
struct LoweringShell {
  using facts = LowerFacts<G, Rewrite>;
  using rewrite = Rewrite;
  using stage = typename facts::stage;
  using rules = typename facts::rules;
  using terminals = typename facts::terms;
  using nonterminals = typename facts::nts;
  using actions = typename facts::rows;

  static const Lowered& table() {
    static const Lowered one = Lowering::lower();
    return one;
  }

  static const auto& cfg() { return table().cfg; }
};

}  // namespace cursiv::detail
