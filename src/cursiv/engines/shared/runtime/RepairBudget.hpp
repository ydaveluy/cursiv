// What a repair search prices and what it may spend: a gesture's cost, the
// tokens that validate a repair, and the work a parse's repair is allowed -- in
// reads, a floor and a share per byte for the whole parse, up to a ceiling
// whatever its length, and the most one search takes of what is left.
#pragma once

#include <algorithm>
#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

// The real tokens a reading takes past its last gesture before the repair
// holds: a search's goal.
inline constexpr std::uint32_t kRepairValidateShifts = 3;

// What a gesture costs: a re-reading keeps the author's bytes where any other
// invents or drops, so it is the least a gesture still owed can cost.
inline constexpr std::uint32_t kRepairGesture = 2;
inline constexpr std::uint32_t kRepairReread = 1;
static_assert(kRepairReread <= kRepairGesture,
              "a re-reading is the least a gesture can cost");

// The bytes of a lexical walk one unit of work pays for.
inline constexpr TextOffset kRepairLexBytes = 32;

// Bounds on work against the document, set on the recovery metric.
inline constexpr std::uint64_t kRepairFloor = std::uint64_t{1} << 16;
inline constexpr std::uint64_t kRepairPerByte = 64;
// What bounds one search's work and memory within the parse's.
inline constexpr std::uint64_t kRepairPerSearch = std::uint64_t{1} << 19;
// What bounds a parse's repair whatever the document's length: its latency.
inline constexpr std::uint64_t kRepairPerParse = std::uint64_t{1} << 21;

struct RepairBudget {
  std::uint64_t floor = kRepairFloor;
  std::uint64_t perByte = kRepairPerByte;
  std::uint64_t perSearch = kRepairPerSearch;
  std::uint64_t perParse = kRepairPerParse;
};

// What a parse of `len` bytes may spend: a bound given as the largest count
// stays the largest, the sum saturating rather than wrapping.
inline std::uint64_t repair_allot(const RepairBudget& b, TextOffset len) {
  constexpr std::uint64_t kAll = ~std::uint64_t{0};
  const std::uint64_t perLen =
      len != 0 && b.perByte > kAll / len ? kAll : b.perByte * len;
  const std::uint64_t sum = perLen > kAll - b.floor ? kAll : b.floor + perLen;
  return std::min(sum, b.perParse);
}

}  // namespace cursiv
