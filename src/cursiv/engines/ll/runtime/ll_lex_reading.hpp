// What a repair reads of the bytes the lexer refused: the token it left open,
// the run no terminal matches, and where a deletion there ends.
#pragma once

#include <cstdint>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/ll/runtime/ll_section.hpp"
#include "cursiv/engines/shared/runtime/LexFailedWalks.hpp"
#include "cursiv/engines/shared/runtime/LexReading.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

template <const auto& T>
inline LexReading ll_lex_reading(const char* src, TextOffset len,
                                 std::uint16_t mode, TextOffset p,
                                 TextOffset& reached) {
  return lex_reading(
      LlLexTape<T>{}, LexOwed{T.lexBodies, T.lexOwed, T.lexOpeners},
      [](const char* text, TextOffset size, std::uint16_t alphabet,
         TextOffset from, TextOffset& halt) {
        return ll_match_text<T>(text, size, alphabet, from, halt);
      },
      src, len, mode, p, reached);
}

// The lexer's refused reading of the token at `p`: the open token its body
// owes, where there is one.
template <const auto& T>
inline LexReading ll_open_reading(LlContext& c, TextOffset p) {
  if (c.reads.spent()) return LexReading{};
  TextOffset reached = p;
  const LexReading r = ll_lex_reading<T>(
      c.src, ll_text_end<T>(c, p, c.len), ll_err_mode<T>(c), p, reached);
  c.reads.pay(p, reached);
  return r;
}

// Where the bytes no terminal matches end: the first place a token reads.
template <const auto& T>
inline TextOffset ll_unmatched_end(const LlContext& c, TextOffset from,
                                    TextOffset len, TextOffset& reached) {
  LexFailedWalks<LlLexTape<T>> failed;
  const auto mode = ll_err_mode<T>(c);
  TextOffset end = from;
  while (end < len) {
    c.stop.poll();
    ++end;
    if (end >= len) break;
    TextOffset halt = end;
    Scan sc;
    if (!failed.fails(LlLexTape<T>{}, c.src, len, mode, end, halt,
                      [&] { c.stop.poll(); }))
      sc = ll_match_text<T>(c.src, len, mode, end, halt);
    reached = std::max(reached, halt);
    if (sc.term != kInvalidSymbol && sc.end > end) break;
  }
  reached = std::max(reached, end);
  return end;
}

template <const auto& T>
inline TextOffset ll_unmatched_end(const LlContext& c, TextOffset from) {
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
