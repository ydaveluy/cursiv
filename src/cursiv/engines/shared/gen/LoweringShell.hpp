// What every lowering names of its grammar, and the table it lowers to once.
#pragma once

#include "cursiv/core/grammar/ImageOf.hpp"
#include "cursiv/engines/shared/gen/LowerDesc.hpp"
#include "cursiv/engines/shared/gen/LowerFacts.hpp"

namespace cursiv::detail {

// `Lower` lowers the image `View` reads, as `Desc` describes it. The closure,
// the symbol split and the action vocabulary are named once: an id is a
// position in one of these lists, so a reader builds its table of thunks from
// the same list.
template <class View, class Lowered,
          Lowered (*Lower)(GrammarImage&, const LowerDesc&),
          const LowerDesc& Desc>
struct LoweringShell {
  using facts = typename View::facts;
  using rewrite = typename View::rewrite;
  using stage = typename facts::stage;
  using rules = typename facts::rules;
  using terminals = typename facts::terms;
  using nonterminals = typename facts::nts;
  using actions = typename facts::rows;

  static Lowered lower() {
    GrammarImage image = grammar_image<View>();
    return Lower(image, Desc);
  }

  static const Lowered& table() {
    static const Lowered one = lower();
    return one;
  }

  static const auto& cfg() { return table().cfg; }
};

}  // namespace cursiv::detail
