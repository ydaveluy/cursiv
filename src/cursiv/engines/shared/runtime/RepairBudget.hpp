// What a repair search prices and what it may spend: a gesture's cost, a
// misspelling's, the tokens that validate a repair, and the work a parse's
// repair is allowed -- in
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
// invents or drops, so it is the least a gesture still owed can cost. The unit
// is fine enough to price a misspelling by how much of its word it changes.
inline constexpr std::uint32_t kRepairUnit = 4;
inline constexpr std::uint32_t kRepairGesture = 2 * kRepairUnit;
inline constexpr std::uint32_t kRepairReread = kRepairUnit;
static_assert(kRepairReread <= kRepairGesture,
              "a re-reading is the least a gesture can cost");
// A word read as a terminal it does not resemble: its deletion and the
// terminal's insertion, made as one gesture.
inline constexpr std::uint32_t kRepairReplace = 2 * kRepairGesture;
// The bytes of a word each fault past its first takes: a word of fewer bytes
// than that is a different word.
inline constexpr std::uint32_t kRepairFaultBytes = 5;

// What reading a word of `length` bytes as a terminal costs, with `faults`
// faults between them: a re-reading for the first fault, and each further
// fault its share of the word, up to `kRepairReplace` where the word is too
// short for them -- one fault up to 4 bytes, two from 5, three from 10.
constexpr std::uint32_t repair_typo_cost(std::uint32_t faults,
                                         std::uint32_t length) {
  const std::uint64_t extra =
      std::uint64_t{faults > 1 ? faults - 1 : 0} * kRepairFaultBytes;
  const std::uint64_t share = std::uint64_t{length} + 1;
  if (extra >= share) return kRepairReplace;
  return kRepairReread + static_cast<std::uint32_t>(
                             (kRepairReplace - kRepairReread) * extra / share);
}
static_assert(repair_typo_cost(1, 3) == kRepairReread);
static_assert(repair_typo_cost(2, 4) == kRepairReplace);
static_assert(repair_typo_cost(2, 5) < kRepairReplace);
static_assert(repair_typo_cost(3, 9) == kRepairReplace);
static_assert(repair_typo_cost(3, 10) < kRepairReplace);
static_assert(repair_typo_cost(2, 12) < 2 * kRepairGesture,
              "a word of 12 bytes two faults off is read as the terminal "
              "before it is replaced by one");

// The bytes of a lexical walk one unit of work pays for.
inline constexpr TextOffset kRepairLexBytes = 32;

// The bytes of lexical walks the work left from `work` to `limit` pays for.
inline std::uint64_t repair_bytes(std::uint64_t work, std::uint64_t limit) {
  const std::uint64_t left = limit > work ? limit - work : 0;
  return left > ~std::uint64_t{0} / kRepairLexBytes ? ~std::uint64_t{0}
                                                    : left * kRepairLexBytes;
}

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
