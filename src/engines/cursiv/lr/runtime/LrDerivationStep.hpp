// One step of the derivation a look-ahead reading took since its cell: the
// readings of one cell share their steps up to where they part.
#pragma once

#include <cstdint>

#include "cursiv/shared/table/Token.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

inline constexpr std::uint32_t kLrNoDerivation = ~std::uint32_t{0};
inline constexpr TextOffset kLrNoEnd = ~TextOffset{0};

// A reduction by `prod` into `state`, or where `prod` is `kLrNoProd` the token
// `sym` shifted into it, read in `mode` over `[begin, end)` where the text
// spells it, `word` its lexical rank where it has one.
struct LrDerivationStep {
  std::uint32_t previous;
  std::uint32_t state;
  std::uint32_t word;
  SymbolId sym;
  std::uint16_t prod;
  TextOffset end;
  TextOffset begin;
  std::uint16_t mode;
};

}  // namespace cursiv::engine
