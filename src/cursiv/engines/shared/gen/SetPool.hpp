// One copy per distinct set among a run of them, and the copy each one names.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/gen/set_dedup.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"

namespace cursiv {

struct SetPool {
  std::vector<DynSet> sets{};
  std::vector<std::uint16_t> of{};

  SetPool() = default;
  SetPool(const DynSet* v, unsigned n) : of(n) {
    sets.reserve(set_dedup(v, n, of.data()));
    for (unsigned i = 0; i < n; ++i)
      if (of[i] == sets.size()) sets.push_back(v[i]);
  }
};

}  // namespace cursiv
