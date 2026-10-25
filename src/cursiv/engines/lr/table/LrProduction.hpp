// The production facts a reading needs; its directives live in the derived
// reduction plan.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

inline constexpr std::uint16_t kLrNoProd = 0xFFFFu;

struct LrProduction {
  std::uint16_t lhs = 0;
  std::uint16_t node = kNoRow;
  std::uint16_t len = 0;
  // Reduced only where condition `gap - 1` holds.
  std::uint8_t gap = 0;
  // A goto of its left side lands by a guard.
  std::uint8_t guarded = 0;
};

}  // namespace cursiv
