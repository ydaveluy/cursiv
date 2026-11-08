// How the LR lowering reads the rules: as materialised, an operator's source
// through the stage.
#pragma once

#include "cursiv/shared/gen/InfixLevel.hpp"
#include "cursiv/shared/gen/InfixOp.hpp"
#include "cursiv/shared/gen/reached.hpp"
#include "cursiv/shared/gen/spec_row.hpp"

namespace cursiv::engine {

struct LrRewrite {
  template <class Materialized, class Declared>
  struct stages {
    using stage = LeadGraphStage<level_rule, Materialized, Declared>;
    using rules = reached_t<stage>;
  };
  template <class St, class Op, auto P>
  using infix_src =
      typename src_rule<St, infix_op_src<Op, P, St::template rule>>::type;
};

}  // namespace cursiv::engine
