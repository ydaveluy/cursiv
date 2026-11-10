// How the LL lowering reads the rules: through the stages that rewrite them, an
// operator's source its own text.
#pragma once

#include "cursiv/grammar/PayloadOf.hpp"
#include "cursiv/ll/gen/LlLoopFold.hpp"
#include "cursiv/shared/gen/InfixLevel.hpp"
#include "cursiv/shared/gen/InfixOp.hpp"
#include "cursiv/shared/gen/reached.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine::llow {

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
  using infix_src = text_payload<infix_op_src<Op, P>>;
};

}  // namespace cursiv::engine::llow
