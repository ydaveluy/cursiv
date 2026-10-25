// A condition that may read the token in hand over a run of terminals with no
// end: a completion cannot step over what it decided, and replays every token
// before the caret.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LowerPools;

struct UnboundedCondition {
  // The rule an author wrote, and the repetition in the lexical pool.
  std::uint16_t rule = kNoRow;
  std::uint16_t run = 0;

  constexpr bool operator==(const UnboundedCondition&) const = default;
};

// What condition `cond` reads without end, as `rule` asks it: one finding per
// rule and spelled run.
void find_unbounded_runs(const LowerPools& c, std::uint16_t cond,
                         std::uint16_t rule,
                         std::vector<UnboundedCondition>& found);

// One sentence: the rule and the run named, and what to write instead. An
// empty `rule` is a rule of the grammar no name reaches.
std::string unbounded_condition_text(const LowerPools& c,
                                     std::string_view rule,
                                     const UnboundedCondition& u);

}  // namespace cursiv
