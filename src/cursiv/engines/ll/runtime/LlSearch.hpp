// A parse's repair search: the readings of the search in flight, the path it
// elected still to be written, and the work the parse and each search may
// spend. Built at the first fault of a parse; nothing here outlives it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/runtime/LlGesture.hpp"
#include "cursiv/engines/ll/runtime/LlLookIndent.hpp"
#include "cursiv/engines/ll/runtime/LlReading.hpp"
#include "cursiv/engines/ll/table/LlSimulation.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/RepairEnds.hpp"
#include "cursiv/engines/shared/runtime/RepairSearch.hpp"
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

// First in a search, so that its simulation stands at the search's address.
struct LlSearchSim {
  LlSimulation sim;
};

struct LlSearch : LlSearchSim, RepairSearch<LlGesture> {
  LlSearch(const LlAtnView& atn, TextOffset len, const RepairBudget& b = {})
      : LlSearchSim{LlSimulation(atn)}, RepairSearch(len, b) {}

  // The visits of the simulation already paid into `work`.
  std::uint64_t visited = 0;

  // What a reading's future depends on, its owed run aside: two readings with
  // the same one gain the same increments of key, the one holding the owed
  // run perhaps more. A fingerprint: it selects the readings that may share
  // a future, and `ll_search_same_future` decides.
  struct Future {
    std::uint64_t a = 0, b = 0;
    bool operator==(const Future&) const = default;
  };
  struct FutureHash {
    std::size_t operator()(const Future& f) const {
      return static_cast<std::size_t>(f.a);
    }
  };
  // The words a position may misspell, and what they were asked for: the
  // alphabets and their terminals, at that position.
  struct TypoMemo {
    TextOffset begin = 0;
    std::vector<std::uint16_t> modes;
    std::vector<std::uint64_t> bits;
    std::vector<Typo> typos;
  };
  // The positions of every reading, one run each.
  std::vector<LlConfig> pool;
  std::vector<LlReading> nodes;
  // The levels every reading stands in: the descent's, copied, and those the
  // readings open. A reading keeps its top alone.
  LlLookIndent indent;
  std::unordered_multimap<Future, std::uint32_t, FutureHash> seen;
  // The readings one read is still carrying forward.
  std::vector<std::uint32_t> out;
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
  std::unordered_multimap<std::uint64_t, TypoMemo> typos;
  std::vector<Typo> cutTypos;
  std::unordered_map<std::uint64_t, TextOffset> drops;
  // What the positions of the reading in hand offer.
  LlFacts facts;

  // The elected path: the gestures to write, the next one to, and the arm of
  // the root's choice the path enters.
  std::vector<LlGesture> plan;
  std::size_t next = 0;
  std::uint16_t arm = 0xFFFFu;
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
