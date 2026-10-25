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
  template <class Materialized, class Declared>
  struct stages {
    using leveled = InfixLevelStage<Materialized, Declared>;
    using rules = reached_t<leveled>;
    using stage = LoopFoldStage<leveled, rules>;
  };
  template <class St, class Op, auto P>
  using infix_src = pay_text<infix_op_src<Op, P>>;
};

}  // namespace cursiv::detail::llow
