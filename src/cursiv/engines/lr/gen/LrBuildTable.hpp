// The growing form of one automaton while it is built: every row the frozen
// table names, plus the rows construction lays for itself and the emitters.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/lr/gen/LrDerivedRows.hpp"
#include "cursiv/engines/lr/gen/LrGotoRows.hpp"
#include "cursiv/engines/lr/gen/LrGuard.hpp"
#include "cursiv/engines/lr/gen/LrIdentity.hpp"
#include "cursiv/engines/lr/gen/LrPeek.hpp"
#include "cursiv/engines/lr/table/LrAction.hpp"
#include "cursiv/engines/lr/table/LrColdTables.hpp"
#include "cursiv/engines/lr/table/LrDerivedSizes.hpp"
#include "cursiv/engines/lr/table/LrFusedRow.hpp"
#include "cursiv/engines/lr/table/LrSizes.hpp"
#include "cursiv/engines/lr/table/LrTableFacts.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"
#include "cursiv/engines/shared/gen/HeadMatcher.hpp"
#include "cursiv/engines/shared/gen/LexRelayGen.hpp"
#include "cursiv/engines/shared/gen/SetPool.hpp"
#include "cursiv/engines/shared/gen/ShapeOverflow.hpp"
#include "cursiv/engines/shared/gen/Vec.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

// What a pass that mints or fuses states does with one per-state column; see
// `LrBuildTable::each_state_column`.
//
// A minted state takes its root's cell. States that fuse hold equal cells:
// minimisation tells states apart by them, and the one cell stands.
struct LrStateCopy {};
// A minted state takes the cell its pass composed for it, handed to the mint.
// States that fuse hold equal cells, as for a copy.
struct LrStateComposed {};
// A minted state takes its root's cell. States that fuse keep the cell they
// all hold, `none` where two part: minimisation does not tell states apart by
// it.
struct LrStateMerge {
  std::uint16_t none;
};

struct LrBuildTable : LrTableFacts, ShapeOverflow, LrDerivedRows {
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
  std::uint32_t nOwed = 0;
  // Those the merge may have made; the first is named below.
  std::uint32_t nMergeInduced = 0;
  std::uint16_t mergeState = 0xffff, mergeTerm = 0xffff;
  // Reduces the merge lets lay an implied token that one of their callers
  // refuses.
  std::uint32_t nImpliedMixed = 0;

  // The copies a pass gave up because the state index was full.
  std::uint32_t nunminted = 0;

  // The first pair that forced a candidate walk, where, and the shortest text
  // it meets on.
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
  // Mode a covers mode b (bit a*nmodes+b): the token a answered is the one b
  // would answer.
  Vec<std::uint64_t> modeSub{};
  // Per mode, the winners whose bytes may hide a rival reading there.
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
  // ...and the word-bounded reading it keeps beside its unbounded one.
  Vec<std::uint16_t> accStand{};
  // ...and the tier of the reading `accBound` bounds.
  Vec<std::uint8_t> accTier{};

  // Rows the table minted, addressed above the grammar's own: a record
  // whose origin is `writes.nchains + k` stands for the pair held here.
  Vec<LrFusedRow> fused{};

  // Terminals with an action, per state.
  Vec<DynSet> follow{};

  LrGotoRows gotoRows{};

  void set_gotos(LrGotoRows rows) {
    if (!rows.canonicalize(cfg.nnts))
      cap_hit("the 16-bit goto count of one nonterminal");
    gotoRows = std::move(rows);
  }

  // The programs of `guard` actions and guarded gotos, and per state what its
  // tag knows of the gap its span opened on and which conditions held there.
  std::vector<LrGuard> guards{};
  Vec<DynSet> tagKnown{};
  Vec<DynSet> tagTruth{};

  // The terminals a state owes, and the longest any production's shortest
  // reading runs.
  Vec<DynSet> closeRow{};
  std::uint64_t closeMax = 0;

  // The sets above as the frozen table holds them, laid by `lr_derive_tables`
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
  // Per state, the symbol it stood after before any pass skipped a
  // pass-through, `kLrNoStateSym` where fused states stood after two.
  Vec<std::uint16_t> stateSym{};
  // ...and the symbol the state under it stood after, where every way into it
  // agrees.
  Vec<std::uint16_t> stateBelow{};
  // Where a skipped pass-through may stand: per production, the positions
  // whose callee the table may skip, `position << 16 | rule`; and per
  // `rule << 16 | symbol` it may stand for, the decisions the skipped run
  // took, before (0) or after (1) its callee.
  std::vector<std::uint32_t> electWatchAt{};
  std::vector<std::uint32_t> electWatch{};
  std::vector<std::uint32_t> electChainKey{};
  std::vector<std::uint32_t> electChainAt{};
  std::vector<std::uint32_t> electChainWord{};
  std::vector<std::uint16_t> electChainPos{};
  // Per production that fused states may name in another's stead, those it
  // may stand for, and its own symbols to find it by.
  std::vector<std::uint32_t> electTwinAt{};
  std::vector<std::uint16_t> electTwin{};
  std::vector<std::uint32_t> electRhsAt{};
  std::vector<std::uint16_t> electRhs{};

  Vec<LrAction> actions{};

  // Every per-state column beside the action and goto rows, with its policy.
  // The passes that mint and fuse states carry each column by it: a new
  // column joins this list, and only a composed one also joins the mint.
  template <class F>
  void each_state_column(F&& f) {
    // The terminals of the action row: a copy's row holds its root's.
    f(follow, LrStateCopy{});
    f(mode, LrStateCopy{});
    f(tagKnown, LrStateCopy{});
    f(tagTruth, LrStateCopy{});
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

  std::uint16_t next_dfa(std::uint16_t s, std::uint8_t c) const {
    return dfaNext[std::size_t{s} * ncls + c];
  }
  bool dfa_bounded(std::uint16_t s) const { return dfaBound[s] != kNoSet; }
  const DynSet& dfa_acc_set(std::uint16_t s) const { return dfaAcc[s]; }
  std::uint16_t dfa_acc_bound(std::uint16_t s, unsigned t) const {
    return accBound[accBoundBase[s] + dfaAcc[s].rank(t)];
  }
  std::uint16_t dfa_acc_stand(std::uint16_t s, unsigned t) const {
    const std::size_t at = accBoundBase[s] + dfaAcc[s].rank(t);
    return at < accStand.size() ? accStand[at] : kNoSet;
  }
  bool standing() const {
    return std::ranges::any_of(accStand.v,
                               [](std::uint16_t s) { return s != kNoSet; });
  }
  std::uint8_t dfa_acc_tier(std::uint16_t s, unsigned t) const {
    return accTier[accBoundBase[s] + dfaAcc[s].rank(t)];
  }
  // The tier `t` reads at `s`: a keyword's reading kept beside it ranks it.
  unsigned dfa_tier(std::uint16_t s, unsigned t) const {
    return dfa_acc_stand(s, t) != kNoSet ? kTierKeyword
                                         : unsigned{dfa_acc_tier(s, t)};
  }
  bool dfa_before(std::uint16_t s, unsigned t, unsigned u) const {
    return lex_term_before(cfg.terms[t], dfa_tier(s, t), t, cfg.terms[u],
                           dfa_tier(s, u), u);
  }
  bool tiered() const {
    return std::ranges::any_of(accTier.v,
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
  // The grammar lays an indentation token or brackets a line, as `kIndented`
  // tells at run time: no mode covers another, and `modeSub` is never built.
  bool indented() const {
    for (unsigned t = 1; t < cfg.nterms; ++t)
      if (cfg.terms[t].laid == kLaidIndent ||
          cfg.terms[t].laid == kLaidDedent || cfg.terms[t].delim != 0)
        return true;
    return false;
  }
  bool mode_covers(unsigned from, unsigned to) const {
    const std::size_t i = std::size_t{from} * nmodes + to;
    return ((modeSub[i >> 6] >> (i & 63u)) & 1u) != 0;
  }

  LrAction action(std::uint32_t i) const { return actions[i]; }
  std::span<const LrAction> action_row(std::uint32_t state) const {
    if (actionCount[state] == 0) return {};
    return {actions.data() + actionBase[state], actionCount[state]};
  }

  LrSizes sizes() const {
    return LrSizes{
        .terms = cfg.nterms,
        .prods = cfg.nprods,
        .states = nstates,
        .modes = nmodes,
        .cls = ncls,
        .dfa = ndfa,
        .dfaAccSets = static_cast<unsigned>(dfaAccPool.sets.size()),
        .dfaResSets = static_cast<unsigned>(dfaResPool.sets.size()),
        .modeWinSets = static_cast<unsigned>(modeWinPool.sets.size()),
        .modeLongers = longer_modes() ? nmodes : 0u,
        .modeBacktracks = static_cast<unsigned>(modeBacktrack.size()),
        .modeFlagSets = static_cast<unsigned>(modeFlagPool.sets.size()),
        .accBounds = static_cast<unsigned>(accBound.size()),
        .relays = nrelays,
        .followSets = static_cast<unsigned>(followPool.sets.size()),
        .closeSets = static_cast<unsigned>(closePool.sets.size()),
        .dfaRelayResSets =
            relayReserves() ? static_cast<unsigned>(dfaRelayResPool.sets.size())
                            : 0u,
        .accStands = standing() ? static_cast<unsigned>(accBound.size()) : 0u,
        .accTiers = tiered() ? static_cast<unsigned>(accBound.size()) : 0u,
        .errModes = static_cast<unsigned>(errModeOf.size()),
        .indented = indented(),
        .insertLists = static_cast<unsigned>(insertAt.size() - 1u),
        .insertWords = static_cast<unsigned>(insertWord.size()),
        .electWords = static_cast<unsigned>(electWord.size()),
        .electWatches = static_cast<unsigned>(electWatch.size()),
        .electChains = static_cast<unsigned>(electChainKey.size()),
        .electChainWords = static_cast<unsigned>(electChainWord.size()),
        .electTwins = static_cast<unsigned>(electTwin.size()),
        .electRhsSyms = static_cast<unsigned>(electRhs.size()),
        .gotoClasses = nconflicts != 0 || multiCand};
  }

  LrDerivedSizes derivedSizes() const {
    return LrDerivedSizes{
        .lexElseRows = static_cast<unsigned>(lexElseRows.size()),
        .typoSucc = static_cast<unsigned>(typoSucc.size()),
        .reduceNames = static_cast<unsigned>(reduceNames.size()),
        .expectItems = static_cast<unsigned>(expectItems.size()),
        .expectPool = static_cast<unsigned>(expectPool.size()),
        .rowLhsRows = static_cast<unsigned>(
            rowLhsBase.empty() ? 0 : rowLhsBase.size() - 1),
        .rowLhs = static_cast<unsigned>(rowLhs.size()),
        .typoReachSets = static_cast<unsigned>(typoReach.sets.size()),
        .afterSets = static_cast<unsigned>(after.sets.size()),
        .modeWantSets = static_cast<unsigned>(modeWant.sets.size())};
  }
};

}  // namespace cursiv
