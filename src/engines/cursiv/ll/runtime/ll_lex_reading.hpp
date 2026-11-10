// What a repair reads of the bytes the lexer refused: the token it left open,
// the run no terminal matches, and where a deletion there ends.
#pragma once

#include <cstdint>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/ll_matcher.hpp"
#include "cursiv/ll/runtime/ll_read.hpp"
#include "cursiv/ll/runtime/ll_section.hpp"
#include "cursiv/shared/runtime/LexReading.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv::engine {

template <const auto& T>
inline LexReading ll_lex_reading(const char* src, TextOffset len,
                                 std::uint16_t mode, TextOffset p,
                                 TextOffset& reached) {
  return lex_reading(LlLexTape<T>{},
                     LexBodies{T.lexBodies, T.lexBodyTerms, T.lexOpeners}, src,
                     len, mode, p, reached);
}

// The lexer's refused reading of the token at `p`: the open token its body
// needs, if any. In a section the text ends at the closing token; the search
// for it stops at the reading's end and counts with it.
template <const auto& T>
inline LexReading ll_open_reading(LlContext& c, TextOffset p) {
  if (c.reads.spent()) return LexReading{};
  const auto mode = ll_err_mode<T>(c.section);
  TextOffset reached = p;
  LexReading r = ll_lex_reading<T>(c.src, c.len, mode, p, reached);
  if (const TextOffset end = ll_text_end<T>(c, p, reached, reached);
      end < c.len) {
    TextOffset again = p;
    r = ll_lex_reading<T>(c.src, end, mode, p, again);
  }
  c.reads.spend(p, reached);
  return r;
}

// The unmatched text at `p` (`lex_unmatched_end`), up to a blank of the mode
// the fault is lexed with.
template <const auto& T>
inline TextOffset ll_unmatched_end(LlContext& c, TextOffset from,
                                   TextOffset len, TextOffset& reached) {
  const auto mode = ll_err_mode<T>(c.section);
  return lex_unmatched_end(
      LlLexTape<T>{}, LexBodies{T.lexBodies, T.lexBodyTerms, T.lexOpeners},
      c.src, len, mode, from,
      [&]([[maybe_unused]] unsigned char b) {
        if constexpr (T.skipSet != nullptr)
          return kLlBytes<T, &LlTable::skipSet>[mode][b];
        return false;
      },
      reached, c.reads, [&c] { c.stop.poll(); });
}

// What a deletion of the token `t` takes: the token; for bytes no terminal
// matches, the whole run of them; for a token left open, only its opener.
template <const auto& T>
inline TextOffset ll_drop_end(LlContext& c, const Token& t) {
  if (t.end != t.begin) return t.end;
  const LexReading r = ll_open_reading<T>(c, t.begin);
  if (r.term != kInvalidSymbol) return lex_cut(t.begin, r);
  TextOffset reached = t.begin;
  return ll_unmatched_end<T>(c, t.begin, c.len, reached);
}

}  // namespace cursiv::engine
