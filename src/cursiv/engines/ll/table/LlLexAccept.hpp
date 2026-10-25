#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LlLexAccept {
  std::uint16_t term = 0;
  std::uint16_t bound = kNoSet;
  // A keyword where the bound is a word class, else a literal where a
  // literal's branch reads it.
  std::uint8_t tier = 0;
  constexpr bool operator==(const LlLexAccept&) const = default;
};

}  // namespace cursiv
