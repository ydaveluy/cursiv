// Lexical fallback, reductions and completion continuations, emitted as data
// so no runtime reader derives them.
#pragma once

#include <array>
#include <cstdint>

#include "cursiv/lr/table/LrDerivedSizes.hpp"
#include "cursiv/lr/table/LrExpectTable.hpp"
#include "cursiv/lr/table/LrLexElse.hpp"
#include "cursiv/lr/table/LrReduceName.hpp"
#include "cursiv/lr/table/LrReducePlan.hpp"
#include "cursiv/lr/table/LrSizes.hpp"
#include "cursiv/lr/table/LrSetStore.hpp"
#include "cursiv/shared/table/ByteSet.hpp"

namespace cursiv::engine {

template <LrSizes S, LrDerivedSizes D>
struct LrDerivedTables {
  // The bytes a run the parse ignores is made of, one bit each, per mode.
  std::array<std::uint64_t, S.modes * 4> skipSet{};
  // The bytes such a run's last byte may read on with: a reader steps back onto
  // that byte and reads on from it.
  std::array<std::uint64_t, S.modes * 4> skipStop{};

  // The fallback election, guarded and ranked: its byte sets are disjoint
  // within a state, and the sentinel closes the last row.
  std::array<std::uint32_t, S.dfa + 1> lexElseBase{};
  std::array<LrLexElse, D.lexElseRows> lexElseRows{};
  // Distinct live successors, ordered by the first byte class reaching each.
  std::array<std::uint32_t, S.dfa + 1> typoSuccBase{};
  std::array<std::uint16_t, D.typoSucc> typoSucc{};

  std::array<LrReducePlan, S.prods> reductions{};
  std::array<LrReduceName, D.reduceNames> reduceNames{};

  LrExpectTable<S.states, D.expectItems, D.expectPool> expect{};
  // The nonterminals a row's record may stand for once reduced: a caret's
  // replay steps over it as one goto. A row whose run is empty is walked.
  std::array<std::uint32_t, D.rowLhsRows + 1> rowLhsBase{};
  std::array<std::uint16_t, D.rowLhs> rowLhs{};
  // Per state, the one state a shift or a goto reaches it from, or 0xFFFF where
  // none or several do or the move may leave a level or a bracket open: a stack
  // whose states each have one down to the start state is known from its top.
  std::array<std::uint16_t, S.states> preds{};

  LrSetStore<S, S.dfa, D.typoReachSets> typoReach{};
  std::array<std::uint8_t, S.dfa> lexBodies{};
  std::array<std::uint16_t, S.dfa> lexBodyTerms{};
  std::array<ByteSet, S.modes> lexOpeners{};
  LrSetStore<S, S.terms, D.afterSets> after{};
  LrSetStore<S, S.modes, D.modeWantSets> modeWant{};
};

}  // namespace cursiv::engine
