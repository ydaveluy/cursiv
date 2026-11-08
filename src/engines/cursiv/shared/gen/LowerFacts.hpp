// What a lowering numbers of its grammar: the rules it reaches, split into
// terminals and the rest, and the actions it publishes.
#pragma once


#include "cursiv/grammar/GrammarUtils.hpp"
#include "cursiv/shared/gen/RuleRows.hpp"
#include "cursiv/shared/gen/reached.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/utils/typelist.hpp"

namespace cursiv::engine {

// What the walk is given, keyed on the grammar alone: the four lists are
// reached through this name, never spelled in the mangled name of every
// `lay<>` instantiated. `Rewrite` is the engine's: the stages it reads the
// rules through, and where an operator reads from.
template <class G, class Rewrite>
struct LowerFacts {
  using resolved = ResolveOverride<G>;
  using materialized = MaterializeCreate<resolved>;
  // The rules `entry`, `ignored` and `hidden` reach, in the symbol order,
  // named by the grammar alone.
  using declared = reached_of<resolved>;
  using predicate_terms = predicate_only_t<resolved>;
  using staged = typename Rewrite::template stages<materialized, declared>;
  using stage = typename staged::stage;
  using rules = typename staged::rules;
  using terms = dedupe_t<
      tl_cat_t<typename sift<stage, rules, true>::type, predicate_terms>>;
  using nts = typename sift<stage, rules, false>::type;
  using rows =
      typename AllRows<stage, rules, Rewrite::template infix_src>::type;
};

}  // namespace cursiv::engine
