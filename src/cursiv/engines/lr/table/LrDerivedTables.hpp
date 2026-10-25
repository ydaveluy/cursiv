// Lexical fallback, reductions and completion continuations, emitted as data
// so no runtime reader derives them.
#pragma once

#include <array>
#include <cstdint>

#include "cursiv/engines/lr/table/LrDerivedSizes.hpp"
#include "cursiv/engines/lr/table/LrExpectTable.hpp"
#include "cursiv/engines/lr/table/LrLexElse.hpp"
#include "cursiv/engines/lr/table/LrReduceName.hpp"
#include "cursiv/engines/lr/table/LrReducePlan.hpp"
#include "cursiv/engines/lr/table/LrSizes.hpp"
#include "cursiv/engines/lr/table/LrSetStore.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"

namespace cursiv {

template <LrSizes S, LrDerivedSizes D>
struct LrDerivedTables {
  // The bytes a run the parse ignores is made of, one bit each, per mode.
  std::array<std::uint64_t, S.modes * 4> skipSet{};

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

  LrSetStore<S, S.dfa, D.typoReachSets> typoReach{};
  std::array<std::uint8_t, S.dfa> lexBodies{};
  std::array<std::uint16_t, S.dfa> lexOwed{};
  std::array<ByteSet, S.modes> lexOpeners{};
  LrSetStore<S, S.terms, D.afterSets> after{};
  LrSetStore<S, S.modes, D.modeWantSets> modeWant{};
};

}  // namespace cursiv
