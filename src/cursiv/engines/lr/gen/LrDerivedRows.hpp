// The growing form of `LrDerivedTables`, laid once the automaton stops
// moving.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/table/LrExpectTable.hpp"
#include "cursiv/engines/lr/table/LrLexElse.hpp"
#include "cursiv/engines/lr/table/LrReduceName.hpp"
#include "cursiv/engines/lr/table/LrReducePlan.hpp"
#include "cursiv/engines/shared/gen/SetPool.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"

namespace cursiv {

struct LrDerivedRows {
  std::vector<std::uint64_t> skipSet{};
  std::vector<std::uint32_t> lexElseBase{};
  std::vector<LrLexElse> lexElseRows{};
  std::vector<std::uint32_t> typoSuccBase{};
  std::vector<std::uint16_t> typoSucc{};
  std::vector<LrReducePlan> reductions{};
  std::vector<LrReduceName> reduceNames{};
  std::vector<std::uint16_t> soleReduce{};
  std::vector<std::uint8_t> readsToken{};
  std::vector<std::uint16_t> gotoClass{};
  std::vector<std::uint32_t> expectBase{};
  std::vector<std::uint16_t> expectItems{};
  std::vector<LrExpect> expectPool{};
  std::vector<std::uint16_t> shiftTerm{};
  std::vector<std::uint32_t> rowLhsBase{};
  std::vector<std::uint16_t> rowLhs{};

  SetPool typoReach{};
  std::vector<std::uint8_t> lexBodies{};
  std::vector<std::uint16_t> lexOwed{};
  std::vector<ByteSet> lexOpeners{};
  SetPool after{};
  SetPool modeWant{};
};

}  // namespace cursiv
