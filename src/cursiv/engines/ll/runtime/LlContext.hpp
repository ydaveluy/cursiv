// What every generated rule reads and writes: the cursor, the one token of
// lookahead, the trivia read past it, and the rules the descent stands in.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/engines/ll/runtime/LlRepairRun.hpp"
#include "cursiv/engines/ll/runtime/LlReturnStack.hpp"
#include "cursiv/engines/ll/runtime/LlSearch.hpp"
#include "cursiv/engines/shared/runtime/CancellationPoll.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/engines/shared/runtime/LexReadBudget.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

class LlMachine;

// Whether the descent invented or skipped anything, and how many times it
// has: what was planned before the last repair no longer says where it goes.
class LlRepairFlag {
 public:
  // One repair event: there is no assignment, so a plain bool cannot stand in.
  void mark() {
    _on = true;
    ++_count;
  }
  operator bool() const { return _on; }
  std::uint32_t count() const { return _count; }

 private:
  bool _on = false;
  std::uint32_t _count = 0;
};

inline constexpr std::uint16_t kLaHeld = 0xFFFEu;

struct LlContext {
  LlContext(std::string_view text, CstBuilder& builder,
            const RepairBudget& repairBudget = {})
      : tape(builder),
        src(text.data()),
        len(static_cast<TextOffset>(text.size())),
        reads(len),
        budget(repairBudget) {}

  CstBuilder& tape;
  const char* src;
  TextOffset len;
  // Read by the cold walks only: prediction past its automaton, repairs.
  CancellationPoll stop;
  // Everything before it is on the tape; the lookahead reaches beyond it.
  TextOffset at = 0;
  // The tokens of no width read since the cursor last moved: two choices at
  // one place are told apart by it where the grammar lays such tokens.
  std::uint32_t lull = 0;

  Token la{};
  // The alphabet `la` was read with, `kInvalidSymbol` while there is none:
  // a token read under one mode is re-read when a point wants another.
  // `kLaHeld` is the token a condition held on: no alphabet reads it again.
  std::uint16_t laMode = kInvalidSymbol;
  bool laValid = false;

  // Unbounded: whitespace and comments alternate, so nothing bounds how many
  // runs sit between two tokens.
  std::vector<Token> pending;
  // How far trivia has been read past `at`: an ignored run is the cursor's
  // advance and never an entry above.
  TextOffset pendEnd = 0;
  // Trivia already written to the tape cannot be rescanned by a later site.
  TextOffset triviaAt = 0;
  std::vector<IndentLevel> indentArena;
  IndentAt indent;
  // End of the last physical token: a backslash inside a comment or string
  // never continues the next logical line.
  TextOffset heldEnd = 0;

  // Every call's exact return and first CST record. Recovery can then leave
  // an unfinished call and charge the author's tokens it actually gives up.
  LlReturnStack stack;
  // The calls the descent hands over, and whether the rule in hand runs in
  // its caller's place: such a rule hands its own calls over.
  LlMachine* machine = nullptr;
  bool nested = false;

  // The generated entry is running: its outermost caller is the document,
  // rather than the unknown caller of a raw prediction helper.
  bool document = false;
  // What a call hands its callee: the precedence a climb stands under.
  unsigned arg = 0;

  // The record last written, which is what a call site states its own
  // directive over.
  NodeId last = kNoNode;

  // The descent gave up: leave the text to its caller. A local give-up ends
  // at its loop; an elected abandon unwinds to its precise return depth.
  bool give = false;
  // An elected abandon unwinds to this return depth. Other give-ups keep
  // their local loop behavior.
  std::size_t abandonDepth = ~std::size_t{0};
  // The first record an elected abandon gave up: a record stating over a
  // node that holds it may have no node left to state over.
  NodeId lost = kNoNode;

  // The parse invented or skipped something: the tree stands, the verdict
  // does not.
  LlRepairFlag repaired;
  // What the repairs may still read of open tokens.
  LexReadBudget reads;
  // The repair search of this parse, built at its first fault.
  std::unique_ptr<LlSearch> search;
  // How many tokens no plan owed have been invented since the last one was
  // read. A repair that opens a construct calls the rules inside it, and those
  // may repair in turn: without this the descent invents its way down a
  // recursion that reads nothing and never returns.
  std::uint16_t invented = 0;
  LlRepairRun run{};
  // The embedded grammar's configuration plus one, zero for the document.
  std::uint8_t section = 0;
  RepairBudget budget;
  // The end a region read, past the gap it crossed to it, and the first
  // record laid after: the nodes that close on that end span the gap.
  TextOffset regionEnd = 0;
  NodeId regionMark = kNoNode;
};

inline void ll_return(LlContext& c) {
  c.stack.pop_back();
  if (c.stack.size() == c.abandonDepth) {
    c.give = false;
    c.abandonDepth = ~std::size_t{0};
    // An abandon that laid no record gave nothing up on the tape.
    if (c.lost >= c.tape.stream().records()) c.lost = kNoNode;
  }
}

}  // namespace cursiv
