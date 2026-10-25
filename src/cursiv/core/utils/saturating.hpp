// The largest count where a product or a sum would wrap: a bound derived from
// a grammar or a document is never smaller for being large.
#pragma once

#include <cstdint>

namespace cursiv {

constexpr std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) {
  return a > ~std::uint64_t{0} - b ? ~std::uint64_t{0} : a + b;
}

constexpr std::uint64_t saturating_mul(std::uint64_t a, std::uint64_t b) {
  return a != 0 && b > ~std::uint64_t{0} / a ? ~std::uint64_t{0} : a * b;
}

}  // namespace cursiv
