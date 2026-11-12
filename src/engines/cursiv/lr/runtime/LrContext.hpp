// What every generated state reads and writes.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/lr/runtime/LrDerivationRoom.hpp"
#include "cursiv/lr/runtime/LrDerivationStep.hpp"
#include "cursiv/lr/runtime/LrGapMemo.hpp"
#include "cursiv/lr/runtime/LrParseStats.hpp"
#include "cursiv/lr/runtime/LrShadow.hpp"
#include "cursiv/lr/runtime/LrSkipMemo.hpp"
#include "cursiv/lr/runtime/LrStack.hpp"
#include "cursiv/lr/runtime/LrTape.hpp"
#include "cursiv/lr/runtime/LrVirtualAt.hpp"
#include "cursiv/lr/runtime/LrWalkMemo.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/IndentLevel.hpp"
#include "cursiv/shared/runtime/LexReadBudget.hpp"
#include "cursiv/shared/runtime/RepairFault.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"

namespace cursiv::engine {

struct LrDrive;
struct LrSearch;

// What stopped the drive for the graph: a cell of the table, or a position
// several terminals match at, whose token is read again.
enum class LrSplit : std::uint8_t { none, cell, position };

struct LrContext {
  LrContext(std::string_view text, CstBuilder& cstBuilder);
  ~LrContext();

  LrTape tape;
  const char* src;
  TextOffset len;
  // Everything before it is on the tape.
  TextOffset committed = 0;
  LrVirtualAt virt;

  Token la{};
  // `kNoMode` is no lookahead held: one field says whether and in which mode.
  std::uint16_t laMode = kNoMode;
  // The fields a shift touches sit in the three cache lines above: a token
  // touches no fourth line.
  // How far plain trivia has been read past `committed`.
  TextOffset pendEnd = 0;
  LrStack stack;
  // Leading trivia waits for a shift so a preceding reduce cannot own it.
  std::vector<Token> pending;
  // Real tokens shifted: what validates a repair.
  std::uint32_t shifts = 0;
  // The readings a cell is resolved with, held across cells.
  std::vector<LrShadow> spec;
  // Per state and kind of decision, the text its look-ahead read before it ran
  // out: short of it a visit goes to the graph directly. An effort bound, not
  // a property of the state.
  std::vector<TextOffset> unresolved;
  // What the last look-ahead that ran out read to.
  TextOffset lookReach = 0;
  // Which arms each reading of a cell descends from, one bit each, at the
  // table's stride.
  std::vector<std::uint64_t> origin;
  // The steps the readings of a cell took, shared where they agree, and the
  // scratch an election between two of them works in.
  std::vector<LrDerivationStep> derivation;
  LrDerivationRoom derivationRoom;
  // Every terminal matching at the cursor where a mode admits several.
  Scan* cand = nullptr;
  LrParseStats stats{};
  std::uint32_t ncand = 0;
  // A cell or a position no look-ahead resolved stopped the drive on it: the
  // graph reads it from the context as it stands.
  LrSplit split = LrSplit::none;

  // Installed by the parser that names the table, so the generated drive
  // reads none: the two-token peek, and how a cell the table leaves open or a
  // position several terminals match at is decided.
  SymbolId (*peekAt)(const LrContext&, std::uint16_t) = nullptr;
  unsigned (*resolveAt)(LrContext&, std::uint32_t, std::uint32_t,
                        unsigned) = nullptr;
  // The candidates are handed over: deciding reads ahead and refills them.
  unsigned (*candAt)(LrContext&, std::uint32_t, const Scan*,
                     unsigned) = nullptr;
  const LrDrive* drive = nullptr;
  LrSearch* search = nullptr;

  // What the repairs written so far cost.
  std::uint64_t cost = 0;
  // The repairs' notes, in the order they are written.
  std::vector<RepairNote> notes;
  std::vector<IndentLevel> indentArena;
  IndentAt indent;
  // What the conditions read of long gaps, for every reading of the parse to
  // ask again at no cost.
  mutable LrGapMemo gaps;
  // What the walks of the lexer's exactness and its re-reads answered.
  mutable LrWalkMemo walkMemo;
  mutable LrSkipMemo skipMemo;
  // Read by the graph and the repair search only, never by the drive.
  CancellationPoll stop;
  // What the deletions may still read of open tokens, over the whole parse.
  mutable LexReadBudget reads;
  // The alternatives one graph may hold, and the parse's graphs together.
  std::uint64_t perGraph = ~std::uint64_t{0};
  std::uint64_t graphBudget = ~std::uint64_t{0};
};

}  // namespace cursiv::engine
