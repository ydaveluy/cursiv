// Word-level helpers the constructions share: the members of a row of words,
// and words folded into a hash.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cursiv {

// Ascending, bit by bit: a set holds hundreds of members, so asking it about
// each in turn is a call per member.
inline void each_bit(const std::uint64_t* w, std::size_t nwords, auto&& f) {
  for (std::size_t k = 0; k < nwords; ++k) {
    std::uint64_t bits = w[k];
    while (bits) {
      f((k << 6) | static_cast<unsigned>(std::countr_zero(bits)));
      bits &= bits - 1;
    }
  }
}

// A finaliser, not a combiner: shift-and-xor barely diffuses, and piles keys
// into a few high-bit buckets.
inline std::uint64_t hash_mix(std::uint64_t h, std::uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ull;
  h *= 0xff51afd7ed558ccdull;
  h ^= h >> 33;
  return h;
}

struct VecHash {
  template <class T>
  std::size_t operator()(const std::vector<T>& k) const {
    std::uint64_t h = 0;
    for (const T x : k) h = hash_mix(h, x);
    return h;
  }
};

}  // namespace cursiv
