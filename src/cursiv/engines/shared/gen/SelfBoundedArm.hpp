// An arm that begins and ends with a call to its own rule reads one text in
// several ways and ranks none of them: the rule is refused, and `infix` or a
// rule per level says what is meant.
#pragma once

#include <string>
#include <vector>

#include "cursiv/core/grammar/GrammarImage.hpp"

namespace cursiv {

struct SelfBoundedArm {
  std::string rule;
  // The arm, as the dump spells it.
  std::string arm;

  bool operator==(const SelfBoundedArm&) const = default;
};

// Each arm once per rule, in the order the rules were reached.
std::vector<SelfBoundedArm> self_bounded_arms(GrammarImage& image);

std::string self_bounded_arm_text(const SelfBoundedArm& a);

}  // namespace cursiv
