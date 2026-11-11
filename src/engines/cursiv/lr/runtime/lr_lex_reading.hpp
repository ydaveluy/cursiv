// The bytes no terminal matches, as the table engine reads them.
#pragma once

#include <cstdint>

#include "cursiv/lr/runtime/lr_matcher.hpp"
#include "cursiv/shared/runtime/LexReadBudget.hpp"
#include "cursiv/shared/runtime/LexReading.hpp"

namespace cursiv::engine {

// The unmatched text at `p` (`lex_unmatched_end`), lexed with the fault mode
// of `mode`, up to a blank of `mode`.
template <const auto& A, class Poll>
TextOffset lr_unmatched_end(const char* src, TextOffset len, std::uint16_t mode,
                            TextOffset p, TextOffset& reached,
                            LexReadBudget& budget, Poll&& poll) {
  return lex_unmatched_end(
      LrLexTape<A>{}, src, len, lr_err_mode<A>(mode), p,
      [mode](unsigned char b) { return lr_skips<A>(mode, b); }, reached, budget,
      poll);
}

}  // namespace cursiv::engine
