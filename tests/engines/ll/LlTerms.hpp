// A table's terminal by its name or by its spelling, or no symbol.
#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/engines/shared/table/Token.hpp"

namespace ll_terms {

template <const auto& T>
constexpr std::uint16_t named(std::string_view name) {
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.term_name(t) == name) return t;
  return cursiv::kInvalidSymbol;
}

template <const auto& T>
constexpr std::uint16_t spelled(std::string_view text) {
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.term_text(t) == text) return t;
  return cursiv::kInvalidSymbol;
}

}  // namespace ll_terms
