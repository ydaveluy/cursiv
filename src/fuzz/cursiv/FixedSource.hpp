// The source that decides nothing: every choice is the first. The lexicon
// spells its separator with it: one canonical spelling, worked out without
// taking any entropy from the caller's source.
#pragma once

#include "cursiv/DecisionSource.hpp"

namespace cursiv::fuzzer::detail {

struct FixedSource final : DecisionSource {
  unsigned choose(DecisionKind, unsigned) override { return 0; }
};

}  // namespace cursiv::fuzzer::detail
