// What a parse met, counted.
#pragma once

#include <cstdint>

namespace cursiv {

// A bound the parse met is named here, never a silent ceiling.
struct LrParseStats {
  // Graphs read: what the look-ahead left open; the stack frames they held.
  std::uint32_t splits = 0;
  std::uint64_t seeded = 0;
  // A re-reading's walk stopped where its search's share ran out.
  std::uint32_t searchStops = 0;
  // A cell ran out of look-ahead -- its depth, its lexes or its steps: at
  // exhaustion the answer is the machine's and not the text's.
  std::uint32_t lookStops = 0;
  // Searches a work bound stopped, races among the cheapest repairs a bound
  // settled before the text did, and pauses repaired past the parse's budget.
  std::uint32_t budgetStops = 0;
  std::uint32_t reachStops = 0;
  std::uint32_t degradedPauses = 0;
  // The closers a degraded search wrote: the run its cap stopped short, and
  // the most readings it held at once -- one, and the attempt in hand.
  std::uint32_t degradedStops = 0;
  std::uint32_t degradedHeld = 0;
  // Stacks a graph dropped for the order the table lays its arms in, the
  // derivations' words leaving them open and the generator having proved that
  // order the election's: the elections that order decides.
  std::uint32_t tableElections = 0;
  // The most alternatives one graph held: what the readings of a split cost.
  std::uint32_t alternatives = 0;
  // Pauses a planned step did not hold at; searches set out from a graph's
  // frozen readings.
  std::uint32_t planDrops = 0;
  std::uint32_t episodeSearches = 0;
  // The units the parse's repairs spent, which its ceiling bounds, and the
  // readings the largest search had room for: what those repairs cost in
  // memory.
  std::uint64_t work = 0;
  std::uint32_t searchRoom = 0;
};

}  // namespace cursiv
