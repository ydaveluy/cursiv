// A parse's repair search: the readings of the search in flight, the path it
// elected still to be written, and the work the parse and each search may
// spend. Reused from one search to the next; nothing here outlives the parse.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/lr/runtime/LrCactus.hpp"
#include "cursiv/lr/runtime/LrConfig.hpp"
#include "cursiv/lr/runtime/LrEdit.hpp"
#include "cursiv/lr/runtime/LrFrame.hpp"
#include "cursiv/lr/runtime/LrStack.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/runtime/LexReading.hpp"
#include "cursiv/shared/runtime/RepairEnds.hpp"
#include "cursiv/shared/runtime/RepairFault.hpp"
#include "cursiv/shared/runtime/RepairSearch.hpp"
#include "cursiv/shared/runtime/TypoWalk.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// The work one cost may spend before a search bounds what its readings
// still cost: most repairs are found before a bound pays off.
inline constexpr std::uint64_t kLrBoundAfter = 1024;

inline constexpr std::uint32_t kLrNoConfig = 0xFFFFFFFFu;
inline constexpr TextOffset kLrNoOpen = 0xFFFFFFFFu;

struct LrSearch : RepairSearch<LrEdit> {
  LrSearch(TextOffset len, const RepairBudget& b,
           const std::vector<IndentLevel>& levels)
      : RepairSearch(len, b), seen(0, FutureHash{}, FutureSame{&levels}) {}

  // What the conditions had read of long gaps when the search last counted.
  std::uint64_t gapsCounted = 0;
  // A span a bound decided, open over the faults written since its search
  // set out; a degraded parse's stays open to the end.
  bool bounded = false;
  TextOffset boundedBegin = 0;
  TextOffset boundedEnd = 0;
  // The least a reading the bound decided could still have finished at: a
  // reading elected below it outranks all of them, and no bound decided it.
  std::uint64_t boundedLeast = ~std::uint64_t{0};
  // Past the budget, a run of insertions is bounded by what its starting stack
  // could need, set there; its work is one search's share. `degradedShifts`
  // tells the run's faults apart.
  TextOffset degradedAt = kLrNoOpen;
  std::uint64_t degradedRun = 0;
  std::uint64_t degradedCap = 0;
  std::uint64_t degradedLimit = 0;
  std::uint32_t degradedShifts = 0xFFFFFFFFu;

  // A reading's stack, position, token in hand, implied tokens, levels and
  // phase; not its free insertions nor the tokens its frames hold. Its levels
  // compare by content, out of the hash.
  struct Future {
    std::uint32_t node;
    TextOffset at;
    Token la;
    std::uint16_t laMode;
    LrVirtualAt virt;
    IndentAt indent;
    std::uint8_t shifts;
    LrRepairPhase phase;
    bool fresh, done;
    bool operator==(const Future&) const = default;
    static Future of(const LrConfig& r) {
      return {r.run.top(),
              r.at,
              r.laMode != kNoMode ? r.la : Token{},
              r.laMode,
              r.virt.seen_from(r.at),
              r.indent,
              r.shifts,
              r.phase,
              r.fresh,
              r.done};
    }
  };
  struct FutureHash {
    std::size_t operator()(const Future& f) const {
      std::uint64_t h = 1469598103934665603ull;
      const auto mix = [&](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; };
      mix(f.node);
      mix(f.at);
      mix(f.la.sym | (std::uint64_t{f.la.begin} << 16));
      mix(f.la.end | (std::uint64_t{f.laMode} << 32));
      mix(f.virt.insertedAt | (std::uint64_t{f.virt.from} << 32));
      mix(f.virt.inserted | (std::uint64_t{f.virt.pending} << 32));
      mix(f.indent.depth);
      mix(f.shifts | (static_cast<std::uint64_t>(f.phase) << 8) |
          (std::uint64_t{f.fresh} << 16) | (std::uint64_t{f.done} << 17));
      return static_cast<std::size_t>(h);
    }
  };
  // Levels written apart are seldom met: their walk stays out of the lookups.
  struct FutureSame {
    const std::vector<IndentLevel>* levels;
    bool operator()(const Future& a, const Future& b) const {
      return a == b || apart(a, b);
    }
    CURSIV_COLD CURSIV_NOINLINE bool apart(const Future& a,
                                           const Future& b) const {
      if (a.indent.head == b.indent.head) return false;
      Future c = a;
      c.indent = b.indent;
      return c == b && indent_same(*levels, a.indent, b.indent);
    }
  };
  using Futures =
      std::unordered_map<Future, std::uint32_t, FutureHash, FutureSame>;
  LrCactus cactus;
  std::vector<LrConfig> configs;
  Futures seen;
  // The configurations one read is still carrying forward.
  std::vector<std::uint32_t> reading;
  std::vector<std::uint32_t> out;
  // Memos of walks their share did not cut: a mode's token at a position, the
  // refused reading and a deletion's extent, the ends re-readings may stop at,
  // and a state's re-readings.
  std::unordered_map<std::uint64_t, Token> lexed;
  std::unordered_map<std::uint64_t, LexReading> readings;
  FlatMap<TextOffset> errorEnds;
  std::unordered_map<std::uint64_t, RepairEnds> ends;
  std::unordered_map<std::uint64_t, std::vector<Typo>> typos;
  std::vector<Typo> cutTypos;
  // Scratch the misspelling walks reuse in turn.
  TypoScratch typoScratch;
  std::unordered_map<TextOffset, Token> written;
  // What the bound reads of the text, never past `horizon`: every mode's tokens
  // at a position, the bytes a mode reading none drops, the re-readings and the
  // least due. `refused`: a lexer stopped mid-token.
  struct Ahead {
    std::vector<Token> tokens;
    std::vector<Token> dropped;
    bool refused = false;
  };
  std::unordered_map<TextOffset, Ahead> ahead;
  std::unordered_map<TextOffset, std::vector<Typo>> misread;
  FlatMap<std::uint32_t> costAhead;
  // The calls of the walk over what a reading still costs, in flight: kept
  // here, not on the native stack, whose depth the text would set. Each
  // resumes at its `phase`.
  enum class CostPhase : std::uint8_t {
    outer,
    outerRet,
    more,
    inner,
    innerRet,
    drop,
    dropRet,
    droppedFirst,
    droppedTypo,
    droppedTypoRet,
  };
  struct CostFrame {
    CostPhase phase = CostPhase::outer;
    SymbolId prev = 0;
    std::uint32_t run = 0;
    std::uint32_t best = 0;
    // The edit cost on the way to the call in flight.
    std::uint32_t cost = 0;
    // The token ahead being taken or dropped, the tokens at its start, and
    // the re-readings of the token a drop stands on.
    std::uint32_t i = 0;
    std::uint32_t j = 0;
    TextOffset p = 0;
    std::uint64_t key = 0;
    Token t{};
    const Ahead* ahead = nullptr;
    const Ahead* more = nullptr;
    const std::vector<Typo>* typos = nullptr;
  };
  std::vector<CostFrame> costStack;
  TextOffset horizon = 0;
  std::uint64_t boundAfter = kLrBoundAfter;
  // A bound in flight, and whether one of its walks could not read a token
  // whole; the positions where one could not, known to read nothing.
  bool bounding = false;
  bool boundShort = false;
  FlatMap<std::uint8_t> unread;
  // What the table does with an invented token before its reading: the
  // reductions it makes (their productions and the states they go to, and the
  // states the stack stands on above its own) and the shift that takes it.
  struct Chain {
    static constexpr std::uint32_t kRoom = 16;
    std::uint16_t prod[kRoom];
    std::uint16_t to[kRoom];
    std::uint16_t above[kRoom];
    std::uint16_t shift = 0;
    std::uint32_t n = 0;
    std::uint32_t attempts = 0;
  };
  Chain chain;
  // A frozen reading a search sets out from: its stack, and how far down its
  // records the author's tokens are counted. Past the tape stand the tokens
  // of a reading the graph has not written: all the author's.
  struct Source {
    const LrStack* frames = nullptr;
    NodeCount scanned = 0;
    std::int64_t count = 0;
  };
  std::vector<Source> sources;
  // What the sources' least cost stands for.
  std::uint64_t costBase = 0;
  // Per frozen frame, the author's tokens its construct holds, counted from
  // the top down as deeper abandons are asked for.
  std::vector<std::int64_t> srcTokens;

  // The elected path: the first step is written at once, the rest at the
  // pauses that match them. `arms` are the choices a step's own token took
  // where it forked.
  struct Step {
    LrEdit g;
    std::uint32_t armBase = 0;
    std::uint32_t armCount = 0;
    // What writing the step adds to the reading's cost.
    std::uint32_t stepCost = 0;
    // The last edit of its fault: its note is added once it is written.
    bool closes = false;
    // The frozen frame under the lowest the reading reached up to this step:
    // below it the stack is the one the search set out from.
    LrFrame kept{};
  };
  std::vector<Step> plan;
  std::vector<std::uint32_t> planArms;
  std::size_t next = 0;
  // Where a path written to its end stopped inside its last fault: a search
  // set out from there, no token read since, goes on with that fault.
  TextOffset openAt = kLrNoOpen;
  // The shifts the context had read when the last edit was written: more
  // since means the fault it left open takes no further edit.
  std::uint32_t wroteShifts = 0;

  // The fault being written: what its edits did so far.
  RepairFault fault;
};

// Where a search's pools stand before an edit is made: what the edit adds
// past it may be given back.
struct LrSearchCheckpoint {
  explicit LrSearchCheckpoint(const LrSearch& s)
      : configs(static_cast<std::uint32_t>(s.configs.size())),
        links(static_cast<std::uint32_t>(s.links.size())),
        cactus(s.cactus.mark()),
        handed(s.handed) {}

  std::uint32_t configs;
  std::uint32_t links;
  LrCactus::Mark cactus;
  std::uint64_t handed;
};

// A span a bound decided opens where the pause it answers stands; `least` is
// what the readings it decided could still have finished at.
inline void lr_search_open(LrSearch& s, TextOffset from, std::uint64_t least) {
  if (least < s.boundedLeast) s.boundedLeast = least;
  if (s.bounded) return;
  s.bounded = true;
  s.boundedBegin = from;
  s.boundedEnd = from;
}

}  // namespace cursiv::engine
