// One reading of a repair search: the positions the graph stands in, where the
// text is read from, and where it stands in the fault it is repairing. It
// writes nothing: only the path the search elects is ever written.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/runtime/LlAhead.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"

namespace cursiv {

inline constexpr std::uint32_t kLlNoNode = 0xFFFFFFFFu;

// The real tokens a plan replay may read to settle the arm it enters.
inline constexpr std::uint32_t kLlArmReach = 64;

// What the positions of a pool run offer: the terminals they read next, the
// ones their shortest ways out begin with and the others, and the alphabets
// they read them under with the terminals each reads.
struct LlFacts {
  // Deletions can share positions but ask their guards at different tokens.
  std::uint32_t reading = kLlNoNode;
  std::uint32_t cfg = kLlNoNode;
  std::uint32_t n = 0;
  std::vector<std::pair<std::uint64_t, std::uint16_t>> written;
  std::vector<std::uint16_t> viable;
  std::vector<std::uint16_t> due;
  std::vector<std::uint16_t> rest;
  std::vector<std::uint16_t> modes;
  std::vector<std::uint64_t> bits;
  std::uint64_t key = 0;
};

struct LlReading {
  // The positions it stands on: a run of the search's pool, sorted.
  std::uint32_t cfg = 0;
  std::uint32_t ncfg = 0;
  LlAhead ahead;
  // The end of the text it has taken: what a condition on a gap reads from.
  TextOffset gapFrom = 0;
  bool done = false;
  // A decision can be replayed only once its surviving readings name one
  // root arm. Terminal roots have that property from their first position.
  bool settled = true;
  // Dominated: never read again.
  bool gone = false;
  // Named by a queue entry: its slot outlives the gesture that laid it.
  bool queued = false;
  // No gesture yet at this point: the next one opens a fault.
  bool fresh = true;
  // Its next token is the descent's token in hand, whose gap the descent
  // crossed under the point's own trivia: it is read where it begins.
  bool inHand = false;

  // The key; the rank is read through `link`.
  std::uint32_t cost = 0;
  std::uint32_t lost = 0;
  std::uint32_t gestures = 0;
  std::uint32_t faults = 0;
  std::uint32_t invented = 0;
  std::uint32_t link = kNoGestureLink;
  // Author tokens read on this path, including re-readings. Unlike shifts,
  // this never resets: call starts use it to price an abandon exactly.
  std::uint32_t tokens = 0;

  // Real tokens since the last gesture, up to `kLlArmReach`.
  std::uint8_t shifts = 0;
  // Where the point stands in `(ins)* (rep | del+)?`; in front of bytes no
  // alphabet reads, one insertion at most.
  std::uint8_t phase = 0;
  // The last gesture was an owed insertion: the next one costs nothing.
  bool owing = false;

  // Stands for the insertions of `genFrom` that are not owed, not yet laid;
  // `genKeep` is the rank base of the terminals left to try. With
  // `genReplace`, for the replacement of its word in hand by the keyword its
  // positions are forced to, laid at rank `genKeep`.
  bool gen = false;
  bool genReplace = false;
  std::uint32_t genFrom = 0;
  std::uint32_t genKeep = 0;
  // A deferred abandon: the call branch, its rank and exact continuation.
  bool genAbandon = false;
  std::uint32_t genRet = 0, genParent = 0, genChoice = 0;
  std::int64_t genStart = 0;
  std::uint16_t genAlt = 0, genOwed = 0;
};

}  // namespace cursiv
