// The grammar facts frozen for a reading; what only construction reads stays
// in LrBuildCfg.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/engines/lr/table/LrLowFacts.hpp"
#include "cursiv/engines/lr/table/LrLowSizes.hpp"
#include "cursiv/engines/lr/table/LrProduction.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

template <LrLowSizes S>
struct LrLowCfg : LrLowFacts {
  std::array<char, S.nameChars> names{};
  std::array<ByteSet, S.sets> sets{};
  // Per set, the lead bytes where the word class's scalar decides, `kNoSet`
  // for a plain class.
  std::array<std::uint16_t, S.sets> setDecode{};
  std::array<TermInfo, S.terms> terms{};
  std::array<LrProduction, S.prods> prods{};
  // The scalars past ASCII the word class holds, sorted and disjoint.
  std::array<unicode::Range, S.wordRanges> wordRanges{};
  // Per set decoding another class, its scalars in `scalarRanges`: `at << 16
  // | count`, zero for the word class's.
  std::array<std::uint32_t, S.scalarRanges != 0 ? S.sets : 0> setScalars{};
  std::array<unicode::Range, S.scalarRanges> scalarRanges{};

  constexpr std::string_view term_name(std::uint16_t t) const {
    return {names.data() + terms[t].nameOff, terms[t].nameLen};
  }
  constexpr std::string_view term_text(std::uint16_t t) const {
    return {names.data() + terms[t].textOff, terms[t].textLen};
  }
};

}  // namespace cursiv
