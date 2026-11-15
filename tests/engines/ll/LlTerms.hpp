// A table's terminal by its name or by its spelling, or no symbol; and the
// parts a table built by hand states it has.
#pragma once

#include <cstdint>
#include <string_view>

#include "cursiv/ll/table/LlTable.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace ll_terms {

constexpr void set_part(cursiv::engine::LlTable& t, cursiv::engine::LlPart p,
                        bool on) {
  const auto bit = 1u << static_cast<unsigned>(p);
  t.parts = on ? (t.parts | bit) : (t.parts & ~bit);
}

template <const auto& T>
constexpr std::uint16_t named(std::string_view name) {
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.term_name(t) == name) return t;
  return cursiv::engine::kInvalidSymbol;
}

template <const auto& T>
constexpr std::uint16_t spelled(std::string_view text) {
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.term_text(t) == text) return t;
  return cursiv::engine::kInvalidSymbol;
}

}  // namespace ll_terms
