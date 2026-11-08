// Where a byte offset falls inside a scalar, and the marks a class's list of
// scalars uses for the scalars before a bound.
#pragma once

#include <cstddef>

#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

// A class's list of scalars names, past them, `kBackScalar + k` for the scalar
// that starts `k` bytes before the byte a bound reads.
inline constexpr char32_t kBackScalar = 0xFFFFFFF0u;
// The bytes the longest well-formed sequence takes.
inline constexpr std::size_t kScalarBytes = 4;

// Whether a well-formed scalar begun before `at` runs past it.
constexpr bool inside_scalar(const char* src, std::size_t at,
                             std::size_t avail) {
  for (std::size_t k = 1; k <= 3 && k <= at; ++k) {
    std::size_t width = 0;
    if (scalar_at(src + at - k, avail - (at - k), width) != kNoScalar &&
        width > k)
      return true;
  }
  return false;
}

}  // namespace cursiv::engine
