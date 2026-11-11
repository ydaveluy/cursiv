// The growing form of one automaton while it is built: every row the frozen
// table names, plus the rows construction fills for itself and the emitters.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/lr/gen/LrBuildCfg.hpp"
#include "cursiv/lr/gen/LrDerivedRows.hpp"
#include "cursiv/lr/gen/LrGotoRows.hpp"
#include "cursiv/lr/gen/LrGuard.hpp"
#include "cursiv/lr/gen/LrPeek.hpp"
#include "cursiv/lr/table/LrAction.hpp"
#include "cursiv/lr/table/LrColdTables.hpp"
#include "cursiv/lr/table/LrFusedRow.hpp"
#include "cursiv/lr/table/LrTableFacts.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"
#include "cursiv/shared/gen/HeadMatcher.hpp"
#include "cursiv/shared/gen/LexRelayGen.hpp"
#include "cursiv/shared/gen/SetPool.hpp"
#include "cursiv/shared/gen/ShapeOverflow.hpp"
#include "cursiv/shared/gen/Vec.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/TermInfo.hpp"

namespace cursiv::engine {

// What a pass that creates or fuses states does with a per-state column; see
// `LrBuildTable::each_state_column`. Copy: a created state takes its root's
// cell, and fused states hold equal cells.
struct LrStateCopy {};
// A created state takes the cell its pass composed for it, given to
// `lr_add_state`. States that fuse hold equal cells, as for a copy.
struct LrStateComposed {};
// A created state takes its root's cell. States that fuse keep the cell they
// all hold, `none` where two part: minimisation does not tell states apart by
// it.
struct LrStateMerge {
  std::uint16_t none;
};

// What one reading of a token fills: the site, and whether the token opens the
// rule that writes it.
struct LrCaretSite {
  ElementId site = kNoElement;
  bool entered = false;
  auto operator<=>(const LrCaretSite&) const = default;
};

// Which states the construction keeps apart: the wider graph is built only
// where the core's merge may have made an ambiguity.
enum class LrIdentity : std::uint8_t { core_plus_lookaheads, core };

// Optional construction counters: what each pass came to.
struct LrBuildWork {
  // The pairs of terminals walked to learn whether one reads over the other.
  std::uint64_t overtakes = 0;
  // The edges read to close the lookaheads, and every read of a set of them.
  std::uint64_t lookaheads = 0;
  // The states read for where a repair's reading goes on.
  std::uint64_t insertions = 0;
  // The continuations of the states, read for whether a state lists them.
  std::uint64_t expects = 0;
  // The actions read for whether a condition may refuse them.
  std::uint64_t refusals = 0;
  // The productions read for the chains their rules hand back.
  std::uint64_t folds = 0;
  // The states read for which class they stand in, and those that moved.
  std::uint64_t minimizes = 0, moves = 0;
};

struct LrBuildTable : LrTableFacts, ShapeOverflow, LrDerivedRows {
  // Where the passes count their work, while the table is built.
  LrBuildWork* work = nullptr;
  std::uint16_t ncanonical = 0;  // states before re-fusion
  LrIdentity identity = LrIdentity::core_plus_lookaheads;
  // Offsets are 32 bits: a grammar can outgrow sixteen.
  std::uint32_t nactions = 0;
  std::uint16_t nmodes = 0;
  std::uint16_t ndfa = 0;
  std::uint16_t nrelays = 0;
  std::uint16_t nfused = 0;

  // Reduce/reduce pairs nothing the grammar wrote orders: the lower
  // production index forks first.
  std::uint32_t nOrdered = 0;
  // Ambiguities, not cells: a merged graph spreads one pair over more cells
  // than a canonical one, and counting cells would penalise the wider graph.
  std::uint32_t nAmbiguities = 0;
  // Those the merge may have made; the first is named below.
  std::uint32_t nMergeInduced = 0;
  std::uint16_t mergeState = 0xffff, mergeTerm = 0xffff;
  // Reduces the merge lets insert an implied token that one of their callers
  // refuses.
  std::uint32_t nImpliedMixed = 0;

  // The copies a pass gave up because the state index was full, each counted
  // once however many edges asked for it.
  std::uint32_t nDroppedCopies = 0;

  // The first pair that forced a candidate walk, where, and the shortest text
  // both match.
  std::uint16_t multiMode = 0, multiTerm = 0, multiInto = 0;
  std::string multiText{};

  LrBuildCfg cfg{};
  // Which rules of `cfg` derive nothing, once the guards have left its bodies.
  std::vector<std::uint8_t> nullable{};
  // A sole returned row, only where the fold fixpoint is complete.
  std::vector<ElementId> soleRow{};
  ChainPool writes{};

  Vec<std::uint32_t> actionBase{};
  Vec<std::uint16_t> actionCount{};
  Vec<std::uint16_t> mode{};

  Vec<LrPeek> peeks{};

  Vec<std::uint16_t> modeStart{};
  // A mode where two terminals can match from one position: the cold path
  // enumerates the candidates.
  Vec<std::uint8_t> modeMulti{};
  // A mode that crosses no trivia: a region's own.
  Vec<std::uint8_t> modeTight{};
  // Per mode, where the grammar embeds others: see `LrFrozenTable`.
  Vec<std::uint16_t> errModeOf{};
  Vec<std::uint16_t> modeEnd{};
  // Per section, the mode its text is read in, whole.
  std::vector<std::uint16_t> sectionErr{};
  // Mode a covers mode b (bit a of b's row of modes): the token a answered is
  // the one b would answer.
  Vec<std::uint64_t> modeSub{};
  // Per mode, the winners whose bytes may hide a competitor reading there.
  Vec<DynSet> modeWin{};
  // Per mode, the mode that lexes what it takes for the length of its token
  // alone, `kNoMode` for none.
  Vec<std::uint16_t> modeLonger{};
  // Empty unless a mode can read arbitrarily far beyond its last match.
  std::vector<std::uint8_t> modeBacktrack{};
  // Generator-only states where an unaccepted reading can cycle.
  std::vector<std::uint8_t> dfaBacktrack{};
  // Per mode, the terminals the lexer takes off a position once no live stack
  // takes them; empty where no state has any.
  Vec<DynSet> modeFlag{};

  // The transition rectangle: ndfa rows of ncls columns.
  Vec<std::uint16_t> dfaNext{};
  // Terminal + 1 ; 0 = not accepting.
  Vec<std::uint16_t> dfaAccept{};
  // Every terminal accepted there, not only the winner.
  Vec<DynSet> dfaAcc{};
  // Terminals a state's keywords reserve away from an identifier match.
  Vec<DynSet> dfaRes{};
  // What a keyword read behind conditions takes, if it stands.
  Vec<DynSet> dfaRelayRes{};
  // The class the byte after this match may not be in, or `kNoSet`.
  Vec<std::uint16_t> dfaBound{};
  Vec<std::uint16_t> dfaRelay{};
  Vec<LexRelayGen> relays{};
  // Per terminal, the words it refuses behind its head and those of them
  // where its token ends, and per mode the terminals refusing some.
  std::vector<HeadMatcher> termHeads{};
  std::vector<HeadMatcher> termTails{};
  std::vector<DynSet> modeHeads{};
  // The same per terminal the state accepts, in the bit order of `dfaAcc[s]`.
  Vec<std::uint32_t> accBoundBase{};
  Vec<std::uint16_t> accBound{};
  // The word-bounded reading it keeps beside its unbounded one.
  Vec<std::uint16_t> accStand{};
  // The priority of the reading `accBound` bounds.
  Vec<std::uint8_t> accPriority{};

  // Rows the table created, addressed above the grammar's own: a record
  // whose origin is `writes.nchains + k` stands for the pair held here.
  Vec<LrFusedRow> fused{};

  // Terminals with an action, per state.
  Vec<DynSet> follow{};

  LrGotoRows gotoRows{};

  void set_gotos(LrGotoRows rows) {
    rows.canonicalize();
    gotoRows = std::move(rows);
  }

  // The programs of `guard` actions and guarded gotos, and per state the
  // conditions its tag knows did not hold on the gap its span opened on.
  std::vector<LrGuard> guards{};
  // The sets of sites that actions and guards name by index. Zero holds none.
  std::vector<std::vector<LrCaretSite>> siteSets{{}};
  Vec<DynSet> tagFalse{};

  // The terminals a state needs, and the longest any production's shortest
  // reading runs.
  Vec<DynSet> closeRow{};
  std::uint64_t closeMax = 0;

  // The sets above as the frozen table holds them, filled by `lr_derive_tables`
  // once none moves.
  SetPool modeWinPool{};
  SetPool modeFlagPool{};
  SetPool dfaAccPool{};
  SetPool dfaResPool{};
  SetPool dfaRelayResPool{};
  SetPool followPool{};
  SetPool closePool{};

  // Per production, the decisions it wrote as a merge compares them; empty
  // where no reading ever merges.
  std::vector<std::uint32_t> electAt{};
  std::vector<std::uint32_t> electWord{};
  std::vector<std::uint16_t> electPos{};
  // Per state, the written order of the terminals a repair may insert there,
  // the list's index plus one in `insertAt`; zero where their ids are.
  Vec<std::uint32_t> insertOf{};
  std::vector<std::uint32_t> insertAt{0};
  std::vector<std::uint16_t> insertWord{};
  // Per state, the symbol it came after before any pass skipped a
  // pass-through, `kLrNoStateSym` where fused states came after two.
  Vec<std::uint16_t> stateSym{};
  // The symbol the state under it comes after, where every way into it
  // agrees.
  Vec<std::uint16_t> stateBelow{};
  // Where a skipped pass-through may stand: per production, each `position <<
  // 16 | rule` whose callee may be skipped; per `rule << 16 | symbol`, the
  // skipped run's decisions, before (0) or after (1) it.
  std::vector<std::uint32_t> electWatchAt{};
  std::vector<std::uint32_t> electWatch{};
  std::vector<std::uint32_t> electChainKey{};
  std::vector<std::uint32_t> electChainAt{};
  std::vector<std::uint32_t> electChainWord{};
  std::vector<std::uint16_t> electChainPos{};
  // Per production that fused states may name in another's stead, those it
  // may stand for as a list shared by all of them, and its own symbols to
  // find it by.
  std::vector<std::uint16_t> electTwinFirst{};
  std::vector<std::uint16_t> electTwinNext{};
  std::vector<std::uint32_t> electRhsAt{};
  std::vector<std::uint16_t> electRhs{};

  Vec<LrAction> actions{};

  // Every per-state column beside the action and goto rows, with its policy,
  // which the passes that create or fuse states follow. A new column joins
  // this list; a composed one also joins `lr_add_state`.
  template <class F>
  void each_state_column(F&& f) {
    // The terminals of the action row: a copy's row holds its root's.
    f(follow, LrStateCopy{});
    f(mode, LrStateCopy{});
    f(tagFalse, LrStateCopy{});
    f(insertOf, LrStateCopy{});
    f(closeRow, LrStateComposed{});
    f(stateSym, LrStateMerge{kLrNoStateSym});
    f(stateBelow, LrStateMerge{kLrNoStateSym});
  }

  // Every state named outside the action and goto rows, with the value that
  // names none: minimisation renumbers it, and sets it to that value once its
  // state is swept.
  template <class F>
  void each_state_ref(F&& f) {
    for (LrGuard& guard : guards)
      for (std::uint16_t& to : guard.to) f(to, LrGuard::kNoState);
    f(mergeState, std::uint16_t{0xffff});
  }

  // The states action `x` goes to: its shift's, or each its guard picks.
  template <class F>
  void each_target(const LrAction& x, F&& f) const {
    if (x.kind == LrActionKind::shift) f(x.arg);
    if (x.kind == LrActionKind::guard) guards[x.arg].each_target(f);
  }
  // The same for a goto edge: its own, and each its guard picks.
  template <class Edge, class F>
  void each_goto_target(const Edge& e, F&& f) const {
    f(e.to);
    if (e.guard != 0) guards[e.guard - 1u].each_target(f);
  }
  // Whether guard `k` has an outcome that leads to no state.
  bool guard_may_fail(std::uint32_t k) const {
    return std::ranges::count(guards[k].to, LrGuard::kNoState) != 0;
  }
  // The first goto of `lhs` from state `from`, null where it has none: a row
  // ascends by nonterminal.
  const LrGotoRows::Edge* goto_edge(unsigned from, unsigned lhs) const {
    const auto row = gotoRows[from];
    const auto at = std::ranges::lower_bound(row, lhs, {}, &LrGotoRows::Edge::nt);
    return at != row.end() && at->nt == lhs ? std::to_address(at) : nullptr;
  }
  // The guard that decides the goto of `lhs` from state `from`, plus one; 0 for
  // a plain goto.
  std::uint32_t goto_guard(unsigned from, unsigned lhs) const {
    const LrGotoRows::Edge* const e = goto_edge(from, lhs);
    return e != nullptr ? e->guard : 0;
  }
  // An empty rule's reduce whose goto the gap decides and may leave nowhere:
  // there the token has no action in state `s`.
  bool goto_may_fail(unsigned s, unsigned prod) const {
    if (cfg.prods[prod].len != 0) return false;
    const std::uint32_t k = goto_guard(s, cfg.prods[prod].lhs);
    return k != 0 && guard_may_fail(k - 1u);
  }
  // Whether a condition may refuse the token action `x` of state `s` takes:
  // a guard that may lead to no state, or a reduce the gap decides.
  bool may_refuse(unsigned s, const LrAction& x) const {
    if (x.kind == LrActionKind::guard) return guard_may_fail(x.arg);
    return x.kind == LrActionKind::reduce &&
           (cfg.prods[x.arg].gap != 0 || goto_may_fail(s, x.arg));
  }

  std::uint16_t next_dfa(std::uint16_t s, std::uint8_t c) const {
    return dfaNext[std::size_t{s} * ncls + c];
  }
  bool dfa_bounded(std::uint16_t s) const { return dfaBound[s] != kNoSet; }
  const DynSet& dfa_acc_set(std::uint16_t s) const { return dfaAcc[s]; }
  std::uint16_t dfa_acc_bound(std::uint16_t s, unsigned t) const {
    return accBound[accBoundBase[s] + dfaAcc[s].rank(t)];
  }
  std::uint16_t dfa_acc_stand(std::uint16_t s, unsigned t) const {
    return accStand[accBoundBase[s] + dfaAcc[s].rank(t)];
  }
  bool standing() const {
    return std::ranges::any_of(accStand.v,
                               [](std::uint16_t s) { return s != kNoSet; });
  }
  std::uint8_t dfa_acc_priority(std::uint16_t s, unsigned t) const {
    return accPriority[accBoundBase[s] + dfaAcc[s].rank(t)];
  }
  // The priority `t` reads at `s`: a keyword's reading kept beside it ranks it.
  unsigned dfa_priority(std::uint16_t s, unsigned t) const {
    return dfa_acc_stand(s, t) != kNoSet ? kKeywordPriority
                                         : unsigned{dfa_acc_priority(s, t)};
  }
  bool dfa_before(std::uint16_t s, unsigned t, unsigned u) const {
    return lex_term_before(cfg.terms[t], dfa_priority(s, t), t, cfg.terms[u],
                           dfa_priority(s, u), u);
  }
  bool prioritized() const {
    return std::ranges::any_of(accPriority.v,
                               [](std::uint8_t t) { return t != 0; });
  }
  const DynSet& dfa_res_set(std::uint16_t s) const { return dfaRes[s]; }
  bool relayReserves() const {
    for (unsigned s = 0; s < ndfa; ++s)
      if (dfaRelayRes[s].any()) return true;
    return false;
  }
  bool longer_modes() const {
    for (unsigned m = 0; m < nmodes; ++m)
      if (modeLonger[m] != kNoMode) return true;
    return false;
  }
  bool flags_mode(unsigned m) const {
    return modeFlag.size() != 0 && modeFlag[m].any();
  }
  // The grammar inserts an indentation token or brackets a line, as `kIndented`
  // tells at run time: no mode covers another, and `modeSub` is never built.
  bool indented() const {
    for (unsigned t = 1; t < cfg.nterms; ++t)
      if (cfg.terms[t].synthetic == kSyntheticIndent ||
          cfg.terms[t].synthetic == kSyntheticDedent || cfg.terms[t].delim != 0)
        return true;
    return false;
  }
  // The 64-bit words of one row of `modeSub`.
  std::size_t mode_sub_words() const { return (nmodes + 63u) / 64u; }
  bool mode_covers(unsigned from, unsigned to) const {
    const std::size_t at = to * mode_sub_words() + (from >> 6);
    return ((modeSub[at] >> (from & 63u)) & 1u) != 0;
  }

  LrAction action(std::uint32_t i) const { return actions[i]; }
  std::span<const LrAction> action_row(std::uint32_t state) const {
    if (actionCount[state] == 0) return {};
    return {actions.data() + actionBase[state], actionCount[state]};
  }
};

}  // namespace cursiv::engine
