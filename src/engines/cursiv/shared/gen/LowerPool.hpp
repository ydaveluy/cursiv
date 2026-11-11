// What both lowering walks keep while they run: the terminals, conditions and
// leaves already built, keyed by their structure.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"

namespace cursiv::engine {

// The actions one column of one element needs, innermost first.
using Acts = std::vector<std::uint16_t>;

inline Acts cat(const Acts& a, const Acts& b) {
  Acts o = a;
  o.insert(o.end(), b.begin(), b.end());
  return o;
}

struct LowerPool {
  std::unordered_map<std::uint64_t, std::uint16_t> termIndex{};
  std::unordered_map<std::uint64_t, std::uint16_t> gapCond{};
  std::unordered_map<std::uint64_t, std::uint16_t> gapLeaf{};
  // Each configuration's skipper node, `kNoLexNode` until a phrase asks it.
  std::vector<std::uint16_t> skipNode{};
  // The rule a diagnostic names: `lhs` may be an auxiliary.
  std::uint16_t owner = kNoElement;
};

}  // namespace cursiv::engine
