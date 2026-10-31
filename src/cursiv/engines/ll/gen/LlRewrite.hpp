// How the LL lowering reads the rules: through the stages that rewrite them, an
// operator's source its own text.
#pragma once

#include "cursiv/core/grammar/PayloadOf.hpp"
#include "cursiv/engines/ll/gen/LlLoopFold.hpp"
#include "cursiv/engines/shared/gen/InfixLevel.hpp"
#include "cursiv/engines/shared/gen/InfixOp.hpp"
#include "cursiv/engines/shared/gen/reached.hpp"

namespace cursiv::detail::llow {

struct Rewrite {
  // The fold rule by its three named parameters alone: a stage's rule has no
  // fourth, and a compiler that reads the defaulted one as a fourth refuses
  // the match.
  template <class S, class G, class R>
  struct fold_rule : loop_fold_rule<S, G, R> {};

  template <class Materialized, class Declared>
  struct stages {
    using leveled = LeadGraphStage<level_rule, Materialized, Declared>;
    using rules = reached_t<leveled>;
    using stage = LeadGraphStage<fold_rule, leveled>;
  };
  template <class St, class Op, auto P>
  using infix_src = pay_text<infix_op_src<Op, P>>;
};

}  // namespace cursiv::detail::llow
