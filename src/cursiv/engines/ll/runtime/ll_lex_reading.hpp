// What a repair reads of the bytes the lexer refused: the token it left open,
// the run no terminal matches, and where a deletion there ends.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/ll/runtime/ll_read.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <const auto& T>
inline LexReading ll_lex_reading(const char* src, TextOffset len,
                                 std::uint16_t mode, TextOffset p,
                                 TextOffset& reached) {
  return lex_reading(LlLexTape<T>{},
                     LexOwed{T.lexBodies, T.lexOwed, T.lexOpeners}, src, len,
                     mode, p, reached);
}

// The lexer's refused reading of the token at `p`: the open token its body
// owes, where there is one. In a section the text ends at the token that
// closes it, which changes the reading only where it starts within what the
// reading read: the search for it stops there, and is paid with the reading.
template <const auto& T>
inline LexReading ll_open_reading(LlContext& c, TextOffset p) {
  if (c.reads.spent()) return LexReading{};
  const auto mode = ll_err_mode<T>(c);
  TextOffset reached = p;
  LexReading r = ll_lex_reading<T>(c.src, c.len, mode, p, reached);
  if (const TextOffset end = ll_text_end<T>(c, p, reached, reached);
      end < c.len) {
    TextOffset again = p;
    r = ll_lex_reading<T>(c.src, end, mode, p, again);
  }
  c.reads.pay(p, reached);
  return r;
}

// The unmatched text at `p` (`lex_unmatched_end`), up to a blank of the mode
// the fault is lexed with.
template <const auto& T>
inline TextOffset ll_unmatched_end(LlContext& c, TextOffset from,
                                   TextOffset len, TextOffset& reached) {
  const auto mode = ll_err_mode<T>(c);
  return lex_unmatched_end(
      LlLexTape<T>{}, LexOwed{T.lexBodies, T.lexOwed, T.lexOpeners}, c.src, len,
      mode, from,
      [&]([[maybe_unused]] unsigned char b) {
        if constexpr (T.skipSet != nullptr) return kLlBlanks<T>[mode][b];
        return false;
      },
      reached, c.reads, [&c] { c.stop.poll(); });
}

template <const auto& T>
inline TextOffset ll_unmatched_end(LlContext& c, TextOffset from) {
  TextOffset reached = from;
  return ll_unmatched_end<T>(c, from, c.len, reached);
}

// What a deletion of the token `t` takes: the token, or of bytes no terminal
// matches the run of them as one -- and of a token left open, only its opener.
template <const auto& T>
inline TextOffset ll_drop_end(LlContext& c, const Token& t) {
  if (t.end != t.begin) return t.end;
  const LexReading r = ll_open_reading<T>(c, t.begin);
  return r.term != kInvalidSymbol ? lex_cut(t.begin, r)
                                  : ll_unmatched_end<T>(c, t.begin);
}

}  // namespace cursiv
