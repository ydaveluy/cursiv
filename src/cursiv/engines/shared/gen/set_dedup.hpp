// One copy per distinct set and an index that names it.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/shared/gen/ChainIndex.hpp"
#include "cursiv/engines/shared/gen/bit_words.hpp"

namespace cursiv {

template <class Set>
std::uint64_t set_hash(const Set& s) {
  std::uint64_t h = 1469598103934665603ull;
  for (const std::uint64_t word : s.w) h = hash_mix(h, word);
  return h;
}

// The distinct values among `n`, in first-occurrence order; `of[i]` names the
// slot of `v[i]`, and may be null.
template <class Set>
unsigned set_dedup(const Set* v, unsigned n, std::uint16_t* of) {
  ChainIndex index;
  index.init(n);
  std::vector<unsigned> rep;
  rep.reserve(n);
  for (unsigned i = 0; i < n; ++i) {
    const std::uint64_t hash = set_hash(v[i]);
    unsigned k = index.first(hash);
    while (k != ChainIndex::kNone && !(v[rep[k]] == v[i])) k = index.after(k);
    if (k == ChainIndex::kNone) {
      k = static_cast<unsigned>(rep.size());
      rep.push_back(i);
      index.add(hash, k);
    }
    if (of) of[i] = static_cast<std::uint16_t>(k);
  }
  return static_cast<unsigned>(rep.size());
}

}  // namespace cursiv
