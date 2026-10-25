// A run nested in a run: a repetition whose item is, or may be nothing but,
// another run -- written in place, or reached through a rule or a choice.
#pragma once

#include <string>
#include <vector>

#include "cursiv/core/grammar/GrammarImage.hpp"

namespace cursiv {

struct NestedRunWarning {
  std::string rule;
  // The outer repetition, as the dump spells it.
  std::string run;

  bool operator==(const NestedRunWarning&) const = default;
};

// Each repetition once per rule, in the order the rules were reached.
std::vector<NestedRunWarning> nested_run_warnings(GrammarImage& image);

std::string nested_run_warning_text(const NestedRunWarning& w);

}  // namespace cursiv
