// The scalar a lead byte starts, decoded and looked up in a class's ranges; a
// byte that starts none is looked up as `kNoScalar`, which only a class that
// holds such bytes lists.
#pragma once

#include <cstddef>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/text/scalar_at.hpp"

namespace cursiv {

constexpr bool lex_scalar_in(const unicode::Range* r, std::size_t n,
                             const char* p, std::size_t avail) {
  std::size_t len = 0;
  return unicode::within(r, n, scalar_at(p, avail, len));
}

// Whether a list holds only `kBackScalar + k`: no scalar of its own to test.
constexpr bool lex_lists_only_back(const unicode::Range* r, std::size_t n) {
  return n != 0 && r[0].lo > kBackScalar && r[n - 1].hi <= kBackScalar + 3;
}

// Whether a bound listing `kBackScalar + k` refuses at `i`: a well-formed
// scalar that starts `k` bytes before it runs past it.
constexpr bool lex_scalar_begun(const unicode::Range* r, std::size_t n,
                                const char* src, std::size_t i,
                                std::size_t len) {
  for (std::size_t k = 1; k <= 3 && k <= i; ++k) {
    const auto lead = static_cast<unsigned char>(src[i - k]);
    if (lead < 0xC2 || (lead < 0xE0 ? 2u : lead < 0xF0 ? 3u : 4u) <= k)
      continue;
    std::size_t width = 0;
    if (unicode::within(r, n, kBackScalar + static_cast<char32_t>(k)) &&
        scalar_at(src + i - k, len - (i - k), width) != kNoScalar && width > k)
      return true;
  }
  return false;
}

}  // namespace cursiv
