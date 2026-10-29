// The lexer's refused reading of a token, as the table engine reads it.
#pragma once

#include <cstdint>

#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/shared/runtime/LexReadBudget.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"

namespace cursiv {

template <const auto& A>
inline constexpr LexOwed kLrLexOwed{A.derived.lexBodies.data(),
                                    A.derived.lexOwed.data(),
                                    A.derived.lexOpeners.data()};

template <const auto& A>
LexReading lr_lex_reading(const char* src, TextOffset len, std::uint16_t mode,
                          TextOffset p, TextOffset& reached) {
  return lex_reading(LrLexTape<A>{}, kLrLexOwed<A>, src, len, mode, p, reached);
}

// The unmatched text at `p` (`lex_unmatched_end`), lexed with the fault mode
// of `mode`, up to a blank of `mode`.
template <const auto& A, class Poll>
TextOffset lr_unmatched_end(const char* src, TextOffset len, std::uint16_t mode,
                            TextOffset p, TextOffset& reached,
                            LexReadBudget& budget, Poll&& poll) {
  return lex_unmatched_end(
      LrLexTape<A>{}, kLrLexOwed<A>, src, len, lr_err_mode<A>(mode), p,
      [mode](unsigned char b) { return lr_skips<A>(mode, b); }, reached, budget,
      poll);
}

}  // namespace cursiv
