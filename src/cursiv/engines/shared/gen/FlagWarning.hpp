// A flag records that its operand matched; one that may read nothing always
// sets the member.
#pragma once

#include <string>
#include <vector>

namespace cursiv {

class WarningFound;

struct FlagWarning {
  std::string rule;
  // As the dump spells it.
  std::string flag;

  bool operator==(const FlagWarning&) const = default;
};

// Each flag once per rule, in the order the rules were reached.
std::vector<FlagWarning> flag_warnings(WarningFound& found);

std::string flag_warning_text(const FlagWarning& w);

}  // namespace cursiv
