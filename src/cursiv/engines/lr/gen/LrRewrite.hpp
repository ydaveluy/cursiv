// How the LR lowering reads the rules: as materialised, an operator's source
// through the stage.
#pragma once

#include "cursiv/engines/shared/gen/InfixLevel.hpp"
#include "cursiv/engines/shared/gen/InfixOp.hpp"
#include "cursiv/engines/shared/gen/reached.hpp"
#include "cursiv/engines/shared/gen/spec_row.hpp"

namespace cursiv {

struct LrRewrite {
  template <class Materialized, class Declared>
  struct stages {
    using stage =
        detail::LeadGraphStage<detail::level_rule, Materialized, Declared>;
    using rules = detail::reached_t<stage>;
  };
  template <class St, class Op, auto P>
  using infix_src = typename detail::src_rule<
      St, infix_op_src<Op, P, detail::stage_of<St>::type::template rule>>::type;
};

}  // namespace cursiv
