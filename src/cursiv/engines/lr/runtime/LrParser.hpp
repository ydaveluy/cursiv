// The entry: an automaton, a document, a tree.
#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstRun.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/engines/lr/runtime/LrContext.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrExit.hpp"
#include "cursiv/engines/lr/runtime/LrFrame.hpp"
#include "cursiv/engines/lr/runtime/LrGss.hpp"
#include "cursiv/engines/lr/runtime/LrGssGraph.hpp"
#include "cursiv/engines/lr/runtime/LrParseStats.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/lr/runtime/LrSearch.hpp"
#include "cursiv/engines/lr/runtime/LrStack.hpp"
#include "cursiv/engines/lr/runtime/lr_lookahead.hpp"
#include "cursiv/engines/lr/runtime/lr_roof.hpp"
#include "cursiv/engines/lr/runtime/lr_search.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"

namespace cursiv {

template <const auto& A>
struct LrParser {
  // A leaf carries the state that shifted it in the record's own stamp, laid
  // out as the table says. The bound is asked here, at the door every caller
  // comes through: past it a state would truncate in silence.
  static_assert(std::bit_width(A.nstates - 1u) <= A.stamp.state_bits(),
                "cursiv: this table's stamp layout cannot name its states");

  // The room a position's candidates fill stands on the C stack, so what
  // bounds it is the stack, not the width of the count.
  static_assert(A.maxCands <= 4096,
                "cursiv: this grammar reads more candidates at one position "
                "than a parse holds room for");

  // A bound row is laid in one bit order and read back in the pooled set's:
  // the winner's entry is where the two are proved equal, and a repair asking
  // after one terminal's guard is otherwise handed another's.
  static_assert(
      [] {
        for (std::size_t s = 0; s < A.dfaAccept.size(); ++s)
          if (A.dfaAccept[s] != 0 &&
              A.dfa_acc_bound(static_cast<std::uint16_t>(s),
                              A.dfaAccept[s] - 1u) != A.dfaBound[s])
            return false;
        return true;
      }(),
      "cursiv: this table's per-acceptance bounds are not in the order their "
      "reader indexes them by");

  // The verdict, and the tree: `accepted` means the start production reduced
  // over the whole document. `budget` is what its repairs may spend. A stop
  // is observed at cold boundaries and leaves the caller's tree empty.
  bool parse(std::string_view text, Cst& cst, LrParseStats* stats,
             const LrDrive* drive, const RepairBudget& budget = {},
             const CancellationToken& cancelToken = kNoCancel) const try {
    throw_if_cancelled(cancelToken);
    cst.reset(text);
    text = cst.text();
    LexSuffixSession<LrLexTape<A>> predicateSession(
        text.data(), static_cast<TextOffset>(text.size()));
    CstBuilder builder(cst);
    builder.reserve_stream(text.size());
    // The drive polls nothing: the stream reads the stop as it fills.
    CstRun::Watch watch(builder.stream(), cancelToken);
    LrContext c(text, builder);
    c.stop = CancellationPoll(cancelToken);
    if constexpr (kTermsRecorded<A.cfg>) c.pending.reserve(8);
    // The room a position's candidates fill, for the life of the parse.
    ScanRoom<(A.maxCands != 0 ? A.maxCands : 1)> room;
    c.cand = room.data();
    c.drive = drive;
    LrSearch search(c.len, budget, c.indentArena);
    c.search = &search;
    install_hooks(c);
    c.stack.push(LrFrame{0, kNoElement, 0, 0, kLrNoWord});
    const bool accepted = run(c) == LrExit::accept;
    c.stop.check();
    lr_search_finish(c, search);
    // The single site a syntax diagnostic is written from: what the
    // elected reading buffered, replayed row by row, deduplicated by the
    // builder.
    for (RepairNote& note : c.notes)
      builder.note_repair(note.kind, note.element, note.begin, note.end,
                          std::move(note.also));
    // The stream states its own count, which is what every reader indexes by.
    builder.bind_stream();
    // A reading no repair could finish names no entry. The root is the
    // last visible record -- a comment after the last token sits on the
    // stream behind it.
    CstRun& stream = builder.stream();
    NodeId root = accepted ? last_visible(stream) : kNoNode;
    if (root != kNoNode)
      root = lr_roof_forest<A.stamp>(stream, root, A.cfg.entryRow);
    if (root == kNoNode)
      builder.bind_entry(0, 0, kNoNode);
    else
      builder.bind_entry(root - stream.at(root).subtreeSize + 1, root + 1,
                         root);
    c.stop.check();
    if (stats != nullptr) *stats = c.stats;
    builder.bind_accepted(accepted);
    return accepted;
  } catch (const OperationCancelled&) {
    cst.reset({});
    throw;
  }

  // What of the table's own machinery this parse reaches. Taking a hook's
  // address binds its whole reading machinery, so a table that cannot reach
  // one names none.
  static void install_hooks(LrContext& c) {
    // Generated modules bind the artifacts together. This low-level entry
    // also accepts test drives, whose required hooks are checked here.
    assert(c.drive != nullptr);
    assert(c.drive->folds != nullptr);
    if constexpr (A.multiCand) assert(c.drive->scanText != nullptr);
    if constexpr (A.npeeks != 0) c.peekAt = &lr_peek2<A>;
    if constexpr (A.nconflicts != 0) c.resolveAt = &lr_resolve<A>;
    if constexpr (A.multiCand) c.candAt = &lr_resolve_cands<A>;
  }

 private:
  // The last record of the derivation: a comment after the last token sits
  // behind the root, and so does a skip region a repair filed there --
  // skipped text names no chain, so neither can be the entry. Asked once,
  // at the bind.
  static NodeId last_visible(const CstRun& stream) {
    for (NodeCount i = stream.records(); i-- > 0;) {
      const CstNode& r = stream.at(i);
      if (r.hidden) continue;
      if (r.recovered && r.subtreeSize == 1 &&
          r.engine == A.stamp.make(kNoElement, 0))
        continue;
      return i;
    }
    return kNoNode;
  }

  // Only the `resolveAt` and `candAt` hooks that `install_hooks` sets can
  // split a parse, so a table without either never needs the graph.
  static constexpr bool kSplits = A.nconflicts != 0 || A.multiCand;
  // The same condition lays the goto classes that those hooks read.
  static_assert(A.cold.gotoClass.size() == (kSplits ? A.nstates : 0u),
                "cursiv: this table's goto classes do not follow its splits");
  struct NoGraph {};

  // The drive reads until it accepts, fails, freezes on a token -- which a
  // search repairs -- or stops on what the look-ahead left open, which the
  // graph reads; both go back to the drive.
  static LrExit run(LrContext& c) {
    // One graph for the whole parse: its buffers outlive each read.
    [[maybe_unused]] std::conditional_t<kSplits, LrGssGraph, NoGraph> graph;
    LrExit e = c.drive->enter(c);
    for (;;) {
      if constexpr (kSplits) {
        if (c.split != LrSplit::none) {
          e = LrGss<A>::read(c, graph);
          continue;
        }
      }
      assert(c.split == LrSplit::none);
      if (e != LrExit::pause) return e;
      e = lr_search_pause<A>(c);
    }
  }
};

template <const auto& A>
inline constexpr LrParser<A> lr_parser{};

}  // namespace cursiv
