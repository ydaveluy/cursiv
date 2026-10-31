// A parse's repair search: the readings of the search in flight, the path it
// elected still to be written, and the work the parse and each search may
// spend. Reused from one search to the next; nothing here outlives the parse.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/lr/runtime/LrCactus.hpp"
#include "cursiv/engines/lr/runtime/LrConfig.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
#include "cursiv/engines/lr/runtime/LrGesture.hpp"
#include "cursiv/engines/lr/runtime/LrStack.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/RepairEnds.hpp"
#include "cursiv/engines/shared/runtime/RepairFault.hpp"
#include "cursiv/engines/shared/runtime/RepairSearch.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"
#include "cursiv/engines/shared/table/FlatMap.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// The work one cost may spend before a search bounds what its readings
// still owe: most repairs are found sooner than a bound pays for itself.
inline constexpr std::uint64_t kLrBoundAfter = 1024;

inline constexpr std::uint32_t kLrNoConfig = 0xFFFFFFFFu;
inline constexpr TextOffset kLrNoOpen = 0xFFFFFFFFu;

struct LrSearch : RepairSearch<LrGesture> {
  LrSearch(TextOffset len, const RepairBudget& b,
           const std::vector<IndentLevel>& levels)
      : RepairSearch(len, b), seen(0, FutureHash{}, FutureSame{&levels}) {}

  // What the conditions had read of long gaps when the search last paid.
  std::uint64_t gapsPaid = 0;
  // A span a bound decided, open over the faults written since its search
  // set out; a degraded parse's stays open to the end.
  bool bounded = false;
  TextOffset boundedBegin = 0;
  TextOffset boundedEnd = 0;
  // The least a reading the bound decided could still have finished at: a
  // reading elected below it outranks all of them, and no bound decided it.
  std::uint64_t boundedLeast = ~std::uint64_t{0};
  // Past the budget, a run of insertions where the parse stands is bounded by
  // what the stack it set out on could owe, fixed there: an insertion that
  // grows the stack grows no bound, and the work it spends is one search's
  // share. `degradedShifts` tells the run's faults apart.
  TextOffset degradedAt = kLrNoOpen;
  std::uint64_t degradedRun = 0;
  std::uint64_t degradedCap = 0;
  std::uint64_t degradedLimit = 0;
  std::uint32_t degradedShifts = 0xFFFFFFFFu;

  // What a reading's future depends on, its free insertions aside: two
  // readings with the same one gain the same increments of key, the one
  // holding fewer free insertions perhaps more. Its levels compare by
  // content, in the parse's arena, and stay out of the hash.
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
  // Memos, of walks their share did not cut: the token at a position under a
  // mode, the lexer's refused reading there and what a deletion takes of it,
  // the ends its re-readings may stop at, a position's re-readings under a
  // state, and the token in hand as the mode that lexes everything reads it.
  std::unordered_map<std::uint64_t, Token> lexed;
  std::unordered_map<std::uint64_t, LexReading> readings;
  FlatMap<TextOffset> errorEnds;
  std::unordered_map<std::uint64_t, RepairEnds> ends;
  std::unordered_map<std::uint64_t, std::vector<Typo>> typos;
  std::vector<Typo> cutTypos;
  // The points the misspelling walks keep, lent to each in turn.
  TypoScratch typoScratch;
  std::unordered_map<TextOffset, Token> written;
  // What the bound reads of the text, never past `horizon`: every mode's
  // tokens at a position and the bytes a mode reading none drops there, every
  // state's re-readings of a position, and the least owed from a position on.
  // `refused`: some mode's lexer was refused in the middle of a token there.
  struct Ahead {
    std::vector<Token> tokens;
    std::vector<Token> dropped;
    bool refused = false;
  };
  std::unordered_map<TextOffset, Ahead> ahead;
  std::unordered_map<TextOffset, std::vector<Typo>> misread;
  FlatMap<std::uint32_t> owes;
  TextOffset horizon = 0;
  std::uint64_t boundAfter = kLrBoundAfter;
  // A bound in flight, and whether one of its walks could not read a token
  // whole; the positions where one could not, known to read nothing.
  bool bounding = false;
  bool boundShort = false;
  FlatMap<std::uint8_t> unread;
  // What the table does with an invented token before its reading: the
  // reductions it makes (their productions and the states they land on, and the
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
  // What the sources' least payment stands for.
  std::uint64_t paidBase = 0;
  // Per frozen frame, the author's tokens its construct holds, counted from
  // the top down as deeper abandons are asked for.
  std::vector<std::int64_t> srcTokens;

  // The elected path: the first step is written at once, the rest at the
  // pauses that match them. `arms` are the choices a step's own token took
  // where it forked.
  struct Step {
    LrGesture g;
    std::uint32_t armBase = 0;
    std::uint32_t armCount = 0;
    // What the reading has paid when it is written, and what it pays.
    std::uint64_t before = 0;
    std::uint32_t paid = 0;
    // The last gesture of its fault: its note is laid once it is written.
    bool closes = false;
    // The frozen frame under the lowest the reading stood on up to this step:
    // below it the stack is the one the search set out from.
    LrFrame kept{};
  };
  std::vector<Step> plan;
  std::vector<std::uint32_t> planArms;
  std::size_t next = 0;
  // Where a path written to its end stopped inside its last fault: a search
  // set out from there, no token read since, goes on with that fault.
  TextOffset openAt = kLrNoOpen;
  // The shifts the context had read when the last gesture was written: more
  // since means the fault it left open takes no gesture now.
  std::uint32_t wroteShifts = 0;

  // The fault being written: what its gestures did so far.
  RepairFault fault;
};

// Where a search's pools stand before a gesture is laid: what the gesture adds
// past it may be given back.
struct LrSearchLaid {
  explicit LrSearchLaid(const LrSearch& s)
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

}  // namespace cursiv
