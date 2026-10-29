// A branch that reads a token past `eof` is unreachable.
// Classification reads the grammar as written; conditions remain zero width.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cursiv {

class WarningFound;

struct DeadBranch {
  std::string rule;
  // As the dump spells it; `choice` holds it where the branch is `eof` alone.
  std::string branch;
  std::string choice;
  // Its place among the branches of its choice, zero outside one.
  std::uint16_t arm = 0, arms = 0;
  // The token read past `eof`, empty where the branch is that token.
  std::string after;

  bool operator==(const DeadBranch&) const = default;
};

// Each branch once per rule, in the order the rules were reached.
std::vector<DeadBranch> dead_branches(WarningFound& found);

std::string dead_branch_text(const DeadBranch& d);

}  // namespace cursiv
