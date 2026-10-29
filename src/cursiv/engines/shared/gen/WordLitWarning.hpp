// A literal a rule reads as a token, with no guard, whose spelling ends in a
// word character: it also matches the start of a longer word.
#pragma once

#include <string>
#include <vector>

namespace cursiv {

class WarningFound;

struct WordLitWarning {
  std::string rule;
  // As the dump spells it, then as the literal does.
  std::string lit;
  std::string text;

  bool operator==(const WordLitWarning&) const = default;
};

// Each literal once per rule, in the order the rules were reached and, within
// one, as written: rule bodies and operator tables, never a terminal, a
// condition or a `through`.
std::vector<WordLitWarning> word_lit_warnings(WarningFound& found);

std::string word_lit_warning_text(const WordLitWarning& w);

}  // namespace cursiv
