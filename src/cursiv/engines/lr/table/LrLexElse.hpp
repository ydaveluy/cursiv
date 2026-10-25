// The winning acceptance for a class of bytes that refuses the DFA state's
// first choice.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct LrLexElse {
  ByteSet bytes;
  SymbolId term = kInvalidSymbol;
  // 0 answers the bytes outright; 1 only where the scalar a lead byte starts
  // is a word, 2 only where it is not; 3 elects as the text is read, `term`
  // unused.
  std::uint8_t scalar = 0;
};

}  // namespace cursiv
