// A parse's repair search: the readings of the search in flight, the path it
// elected still to be written, and the work the parse and each search may
// spend. Built at the first fault of a parse; nothing here outlives it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/runtime/LlGesture.hpp"
#include "cursiv/engines/ll/runtime/LlReading.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/runtime/RepairEnds.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// The positions a simulation visits that one unit of work pays for.
inline constexpr std::uint64_t kLlVisitsPerWork = 4;

// The scan of an alphabet at a position not yet made.
inline constexpr TextOffset kLlUnscanned = 0xFFFFFFFFu;

// What the searches of a thread have spent.
struct LlSearchWork {
  std::uint64_t work = 0;

  static LlSearchWork& current() {
    static thread_local LlSearchWork spent;
    return spent;
  }
};

// Where a search starts: the choice whose arms it reads from, or the position
// whose token is missing.
struct LlRoot {
  std::uint16_t decision = kNoRow;
  std::uint16_t node = kNoRow;
  bool operator==(const LlRoot&) const = default;
};

struct LlSearch {
  LlSearch(const LlAtnView& atn, TextOffset len, const RepairBudget& b = {})
      : sim(atn), budget(repair_allot(b, len)), perSearch(b.perSearch) {}

  LlSimulation sim;
  std::uint64_t budget;
  std::uint64_t perSearch;
  std::uint64_t work = 0;
  // The visits of the simulation already paid into `work`.
  std::uint64_t visited = 0;
  // Where the search in flight stops: its share of what the parse has left.
  std::uint64_t limit = 0;
  // The parse's budget answered: every later fault is repaired by the first
  // gesture that holds.
  bool degraded = false;

  struct Entry {
    std::uint32_t f, lost, faults, invented, seq, at;
  };
  // What a reading's future depends on, its owed run aside: two readings with
  // the same one gain the same increments of key, the one holding the owed
  // run perhaps more.
  struct Future {
    std::uint64_t a = 0, b = 0;
    bool operator==(const Future&) const = default;
  };
  struct FutureHash {
    std::size_t operator()(const Future& f) const {
      return static_cast<std::size_t>(f.a);
    }
  };
  // The positions of every reading, one run each.
  std::vector<LlConfig> pool;
  std::vector<LlReading> nodes;
  std::vector<LlGesture> links;
  // Links a dominated reading handed to the one that kept its future.
  std::uint64_t handed = 0;
  GestureRank rank;
  std::vector<Entry> heap;
  std::uint32_t seq = 0;
  std::unordered_map<Future, std::uint32_t, FutureHash> seen;
  // The readings one read is still carrying forward, and the goals of the
  // least cost.
  std::vector<std::uint32_t> out;
  std::vector<std::uint32_t> goals;
  // Scratch for the terminals a step reads and the ones a position offers.
  std::vector<std::uint16_t> terms;
  std::unordered_map<std::uint32_t, std::uint32_t> depths;
  std::vector<std::uint32_t> chain;
  // Prefix tokens in actual calls, counted lazily backward from the pause.
  std::vector<std::uint32_t> sourceTokens;
  std::uint32_t sourceScanned = 0, sourceCount = 0;
  // Memos, of walks their share did not cut: the token at a position, what
  // each alphabet reads there, the lexer's refused reading there, the ends its
  // re-readings may stop at, the words a position may misspell, and where a
  // deletion there ends.
  std::unordered_map<std::uint64_t, Token> lexed;
  std::unordered_map<TextOffset, std::vector<Scan>> scans;
  std::unordered_map<std::uint64_t, LexReading> readings;
  std::unordered_map<std::uint64_t, RepairEnds> ends;
  std::unordered_map<std::uint64_t, std::vector<Typo>> typos;
  std::vector<Typo> cutTypos;
  std::unordered_map<std::uint64_t, TextOffset> drops;
  // What the positions of the reading in hand offer.
  LlFacts facts;

  // The elected path: the gestures to write, the next one to, and the arm of
  // the root's choice the path enters. `also` are the terminals the first
  // insertion could as cheaply have been.
  std::vector<LlGesture> plan;
  std::size_t next = 0;
  std::uint16_t arm = 0xFFFFu;
  std::vector<ElementId> also;
  // The root and the pause the plan was made for: its arm is that root's
  // alone.
  bool planned = false;
  LlRoot root;
  TextOffset madeAt = 0;
  TextOffset madeBegin = 0;
};

// Where a search's pools stand before a gesture is laid: what the gesture adds
// past it may be given back.
struct LlSearchLaid {
  explicit LlSearchLaid(const LlSearch& s)
      : pool(s.pool.size()),
        nodes(static_cast<std::uint32_t>(s.nodes.size())),
        links(static_cast<std::uint32_t>(s.links.size())),
        handed(s.handed) {}

  std::size_t pool;
  std::uint32_t nodes;
  std::uint32_t links;
  std::uint64_t handed;
};

}  // namespace cursiv
