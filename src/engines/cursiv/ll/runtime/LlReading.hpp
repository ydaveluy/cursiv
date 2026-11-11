// One reading of a repair search: the positions the graph stands in, where the
// text is read from, and where it stands in the fault it is repairing. It
// writes nothing: only the path the search elects is ever written.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/ll/runtime/LlAhead.hpp"
#include "cursiv/shared/runtime/RepairSearch.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

inline constexpr std::uint32_t kLlNoNode = 0xFFFFFFFFu;

// The real tokens a plan replay may read to decide the arm it enters.
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

struct LlReading : RepairReading {
  // The positions it stands on: a run of the search's pool, sorted.
  std::uint32_t cfg = 0;
  std::uint32_t ncfg = 0;
  LlAhead ahead;
  // The end of the text it has taken: what a condition on a gap reads from.
  TextOffset gapFrom = 0;
  // Its surviving readings name one root arm, the one a plan replay enters.
  // Terminal roots have that property from their first position.
  bool decided = true;
  // Its next token is the descent's token in hand, whose gap the descent
  // crossed under the point's own trivia: it is read where it begins.
  bool inHand = false;
  // The last edit was a required insertion: the next one costs nothing.
  bool afterRequired = false;
  // Author tokens read on this path, including re-readings. Unlike shifts,
  // this never resets: call starts use it to cost an abandon exactly. A race
  // costs nothing and holds the place in the text here (`LlSearchRace`).
  std::uint32_t tokens = 0;

  // A generator's payload: the reading it was made from and what it keeps,
  // the rank base of the terminals left for insertions, the rank for a
  // replacement, the depth for an abandon.
  std::uint32_t genFrom = 0;
  std::uint32_t genKeep = 0;
  // Of an abandon: the call branch, its rank and exact continuation.
  std::uint32_t genRet = 0, genParent = 0, genChoice = 0;
  std::int64_t genStart = 0;
  std::uint16_t genAlt = 0, genMissing = 0;
};

}  // namespace cursiv::engine
