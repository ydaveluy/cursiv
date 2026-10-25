// What the program knows about one rule: its name, its body, and the record
// its own call lays.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LlRuleInfo {
  std::uint16_t nameOff = 0, nameLen = 0;
  // The root of its body in the node pool.
  std::uint16_t body = kNoRow;
  // The climb this rule is, where it is one: a precedence table is not a body
  // but a table of operators over an operand rule.
  std::uint16_t climb = kNoRow;

  constexpr bool operator==(const LlRuleInfo&) const = default;
};

}  // namespace cursiv
