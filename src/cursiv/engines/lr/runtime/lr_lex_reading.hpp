// The lexer's refused reading of a token, as the table engine reads it.
#pragma once

#include <cstdint>

#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/shared/runtime/LexFailedWalks.hpp"
#include "cursiv/engines/shared/runtime/LexReadBudget.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <const auto& A>
LexReading lr_lex_reading(const char* src, TextOffset len, std::uint16_t mode,
                          TextOffset p, TextOffset& reached) {
  return lex_reading(
      LrLexTape<A>{},
      LexOwed{A.derived.lexBodies.data(), A.derived.lexOwed.data(),
              A.derived.lexOpeners.data()},
      [](const char* s, TextOffset n, std::uint16_t m, TextOffset from,
         TextOffset& halt) { return lr_match_halt<A>(s, n, m, from, halt); },
      src, len, mode, p, reached);
}

// The unmatched text at `p`: the bytes where no token of any mode starts, up
// to a blank of `mode` or a line end -- one token, however long. `reached`
// rises to how far the walks read; the readings of open tokens are paid from
// `budget`.
template <const auto& A>
TextOffset lr_unmatched_end(const char* src, TextOffset len, std::uint16_t mode,
                            TextOffset p, TextOffset& reached,
                            LexReadBudget& budget) {
  const std::uint16_t err = lr_err_mode<A>(mode);
  const ByteSet& opens = A.derived.lexOpeners[err];
  LexFailedWalks<LrLexTape<A>> failed;
  TextOffset q = p + 1;
  while (q < len && !lex_line_end_at(src, q) &&
         !lr_skips<A>(mode, static_cast<unsigned char>(src[q]))) {
    TextOffset halt = q;
    Scan s;
    if (!failed.fails(LrLexTape<A>{}, src, len, err, q, halt))
      s = lr_match_halt<A>(src, len, err, q, halt);
    if (halt > reached) reached = halt;
    if (s.term != kInvalidSymbol && s.end > q) break;
    if (!budget.spent() && opens.has(static_cast<unsigned char>(src[q]))) {
      TextOffset read = q;
      const LexReading r = lr_lex_reading<A>(src, len, err, q, read);
      budget.pay(q, read);
      if (read > reached) reached = read;
      if (r.term != kInvalidSymbol) break;
    }
    ++q;
  }
  if (q > reached) reached = q;
  return q;
}

}  // namespace cursiv
