// The draws a random grammar and its documents are made of: a seed names one
// grammar on every platform, so the generator and the lane agree on it.
#pragma once

#include <cstdint>

namespace cursiv {

struct RgramRandom {
  std::uint64_t state = 0;

  // splitmix64.
  std::uint64_t next() {
    std::uint64_t z = state += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }
  unsigned below(unsigned n) { return static_cast<unsigned>(next() % n); }
  unsigned range(unsigned lo, unsigned hi) { return lo + below(hi - lo + 1); }
  double unit() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
};

}  // namespace cursiv
