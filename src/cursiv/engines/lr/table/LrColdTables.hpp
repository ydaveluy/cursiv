// What only repair walks: the terminals a state admits and what its open
// items owe. Its own object, so a step never shares a cache line with it.
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "cursiv/engines/lr/table/LrBitSet.hpp"
#include "cursiv/engines/lr/table/LrSetStore.hpp"
#include "cursiv/engines/lr/table/LrSizes.hpp"

namespace cursiv {

// A state fused from states that stood after two symbols.
inline constexpr std::uint16_t kLrNoStateSym = 0xFFFFu;
// The word past every turn's: a loop that stops loses to one that goes on.
inline constexpr std::uint32_t kLrExitWord = ~std::uint32_t{0};

template <LrSizes S>
struct LrColdTables {
  using TermSet = LrBitSet<S.termBits()>;
  using AccBoundOffset = std::conditional_t<(S.accBounds <= 0xFFFFu),
                                            std::uint16_t, std::uint32_t>;

  // Terminals with an action, per state, one set per distinct row.
  LrSetStore<S, S.states, S.followSets> follow{};

  // The guard each accepted terminal declares: a row per DFA state, one entry
  // per member of `dfaAcc[s]`, in bit order.
  std::array<AccBoundOffset, S.dfa + 1u> accBoundBase{};
  std::array<std::uint16_t, S.accBounds> accBound{};
  std::array<std::uint16_t, S.accStands> accStand{};
  std::array<std::uint8_t, S.accTiers> accTier{};

  // The production every reduce of a state names, if one.
  std::array<std::uint16_t, S.states> soleReduce{};
  std::array<std::uint8_t, S.states> readsToken{};
  // States that take the same gotos and hold the same conditions of the gap
  // are one class: below its top, a frame is read for nothing else. Laid
  // only where a reading can split.
  std::array<std::uint16_t, S.gotoClasses ? S.states : 0> gotoClass{};

  // The terminals a state owes, one set per distinct row, and the most
  // tokens any production's shortest reading holds.
  std::array<TermSet, S.closeSets> closePool{};
  std::array<std::uint16_t, S.states> closeOf{};
  std::uint64_t closeMax = 0;

  // The written order of the terminals a repair may insert in a state, where
  // it is not their ids': state `s` with `k = insertOf[s]` names the terminals
  // `insertWord[insertAt[k - 1], insertAt[k])`; zero names none.
  std::array<std::uint32_t, S.insertWords != 0 ? S.states : 0> insertOf{};
  std::array<std::uint32_t, S.insertWords != 0 ? S.insertLists + 1 : 0>
      insertAt{};
  std::array<std::uint16_t, S.insertWords> insertWord{};

  // The decisions each production wrote, in the order a reading takes them:
  // production `p`'s are `electWord[electAt[p], electAt[p + 1])`, each before
  // the child `electPos` names.
  std::array<std::uint32_t, S.electWords != 0 ? S.prods + 1 : 0> electAt{};
  std::array<std::uint32_t, S.electWords> electWord{};
  std::array<std::uint16_t, S.electWords> electPos{};
  // The symbol each state stood after before the passes skipped
  // pass-throughs into it: what an edge into it stands for.
  std::array<std::uint16_t, S.electWords != 0 ? S.states : 0> electSym{};
  // ...and the symbol the state under it stood after, where every way into it
  // agrees: what the frame under it stands for.
  std::array<std::uint16_t, S.electWords != 0 ? S.states : 0> electBelow{};
  // Per action, whether its cell is ordered as the election orders it for
  // every text: a bit of word `action / 64`.
  std::array<std::uint64_t, S.orderWords> orderExact{};
  // A pass-through the table skips: per production `electWatch[electWatchAt[p],
  // electWatchAt[p + 1])`, each `position << 16 | rule` whose callee may stand
  // there; per `rule << 16 | symbol`, ascending, the decisions of the skipped
  // run, before (0) or after (1) the callee.
  std::array<std::uint32_t, S.electWatches != 0 ? S.prods + 1 : 0>
      electWatchAt{};
  std::array<std::uint32_t, S.electWatches> electWatch{};
  std::array<std::uint32_t, S.electChains> electChainKey{};
  std::array<std::uint32_t, S.electChains != 0 ? S.electChains + 1 : 0>
      electChainAt{};
  std::array<std::uint32_t, S.electChainWords> electChainWord{};
  std::array<std::uint16_t, S.electChainWords> electChainPos{};
  // A production fused states may name in another's stead: those it may stand
  // for, `electTwin[electTwinAt[p], electTwinAt[p + 1])`, each found by its
  // symbols, `electRhs[electRhsAt[q], electRhsAt[q + 1])`.
  std::array<std::uint32_t, S.electTwins != 0 ? S.prods + 1 : 0> electTwinAt{};
  std::array<std::uint16_t, S.electTwins> electTwin{};
  std::array<std::uint32_t, S.electTwins != 0 ? S.prods + 1 : 0> electRhsAt{};
  std::array<std::uint16_t, S.electRhsSyms> electRhs{};

  constexpr const TermSet& close_row(std::uint16_t s) const {
    return closePool[closeOf[s]];
  }
};

}  // namespace cursiv
