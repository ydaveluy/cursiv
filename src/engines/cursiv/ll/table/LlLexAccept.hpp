#pragma once

#include <cstdint>

#include "cursiv/shared/table/TermInfo.hpp"

namespace cursiv::engine {

struct LlLexAccept {
  std::uint16_t term = 0;
  std::uint16_t bound = kNoSet;
  // A keyword where the bound is a word class, else a literal where a
  // literal's branch reads it.
  std::uint8_t priority = 0;
  constexpr bool operator==(const LlLexAccept&) const = default;
};

}  // namespace cursiv::engine
