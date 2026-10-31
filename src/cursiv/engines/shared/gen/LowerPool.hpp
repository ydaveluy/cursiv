// What both lowering walks keep while they run: the terminals, conditions and
// leaves already laid, told by the structure that spells them.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"

namespace cursiv {

struct LowerPool {
  std::unordered_map<std::uint64_t, std::uint16_t> termIndex{};
  std::unordered_map<std::uint64_t, std::uint16_t> gapCond{};
  std::unordered_map<std::uint64_t, std::uint16_t> gapLeaf{};
  // Each configuration's skipper node, `kNoLexNode` until a phrase asks it.
  std::vector<std::uint16_t> skipNode{};
  // The rule a diagnostic names: `lhs` may be an auxiliary.
  std::uint16_t owner = kNoElement;
};

}  // namespace cursiv
