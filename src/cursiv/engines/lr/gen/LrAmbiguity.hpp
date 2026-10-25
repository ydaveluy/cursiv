// What the lowered grammar shows ambiguous with no table to consult: two runs
// of one item side by side. They read some text two ways whatever the table
// does.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LrBuildCfg;

struct LrAmbiguity {
  // The rule an author wrote, and the symbol the runs repeat.
  std::uint16_t rule = kNoRow;
  std::uint16_t item = kNoRow;

  constexpr bool operator==(const LrAmbiguity&) const = default;
};

// One finding per rule and item, in the order the productions stand.
std::vector<LrAmbiguity> lr_find_ambiguities(const LrBuildCfg& c);

// One sentence: the rule named, and what to write instead.
std::string lr_ambiguity_text(const LrBuildCfg& c, const LrAmbiguity& a);

}  // namespace cursiv
