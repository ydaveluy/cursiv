// One production; its positions are a run of the configuration's slot pool.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LrLowProd {
  std::uint16_t lhs = 0;  // nonterminal index
  // The actions this reduce applies to the record it lays; `kNoRow` when it
  // lays none and hands them to the record below instead.
  std::uint16_t node = kNoRow;
  std::uint32_t at = 0;  // where its positions start in the pool
  std::uint16_t len = 0;

  // Whether this reduce can meet a chain already in hand.
  bool folds = false;
  // Reduced only where condition `gap - 1` holds; 0 for every other.
  std::uint8_t gap = 0;

  constexpr bool operator==(const LrLowProd&) const = default;
};

}  // namespace cursiv
