// One copy per distinct set, named by its place in the order first met.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/shared/gen/ChainIndex.hpp"
#include "cursiv/shared/gen/bit_words.hpp"
#include "cursiv/shared/table/DynSet.hpp"

namespace cursiv::engine {

template <class Set>
std::uint64_t set_hash(const Set& s) {
  std::uint64_t h = 1469598103934665603ull;
  for (const std::uint64_t word : s.w) h = hash_mix(h, word);
  return h;
}

struct SetPool {
  std::vector<DynSet> sets{};
  // The copy each set of the run it was made of names.
  std::vector<std::uint16_t> of{};

  SetPool() { _index.init(64); }
  SetPool(const DynSet* v, unsigned n) : SetPool() {
    for (unsigned i = 0; i < n; ++i)
      of.push_back(static_cast<std::uint16_t>(name(v[i])));
  }

  // The copy of `s`, made where none holds the same.
  std::uint32_t name(const DynSet& s) {
    const std::uint64_t h = set_hash(s);
    for (unsigned i = _index.first(h); i != ChainIndex::kNone;
         i = _index.after(i))
      if (sets[i] == s) return i;
    const auto id = static_cast<std::uint32_t>(sets.size());
    sets.push_back(s);
    _index.add(h, id);
    return id;
  }

 private:
  ChainIndex _index;
};

}  // namespace cursiv::engine
