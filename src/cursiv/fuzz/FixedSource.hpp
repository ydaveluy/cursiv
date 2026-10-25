// The ribbon that decides nothing: every choice is the first. What a
// layout fixpoint uses -- a separator it is forced to add must not eat the
// caller's entropy -- and what a probe wanting one canonical spelling uses.
#pragma once

#include "cursiv/fuzz/DecisionSource.hpp"

namespace cursiv::fuzzer {

struct FixedSource final : DecisionSource {
  unsigned choose(DecisionKind, unsigned) override { return 0; }
};

}  // namespace cursiv::fuzzer
