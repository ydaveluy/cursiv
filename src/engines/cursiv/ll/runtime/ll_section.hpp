// What a descent reads inside a section a grammar opens with `section<>`: the
// embedded grammar's own terminals, up to the token that closes the section,
// where that grammar alone would see the end of its text.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "cursiv/text/TextOffset.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/ll_matcher.hpp"
#include "cursiv/ll/table/LlTable.hpp"
#include "cursiv/shared/runtime/LexFailedWalks.hpp"
#include "cursiv/shared/runtime/Scan.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv {

// The mode a fault is lexed with: every terminal, or inside a section every
// terminal of its grammar and the token that closes it.
template <const auto& T>
constexpr std::uint16_t ll_err_mode(const LlContext& c) {
  return T.nsections != 0 && T.sectionErr != nullptr && c.section != 0
             ? T.sectionErr[c.section - 1u]
             : T.errMode;
}

// Whether `sym` ends the text the descent reads: the end of input, or in a
// section the token that closes it.
template <const auto& T>
constexpr bool ll_ends(const LlContext& c, std::uint16_t sym) {
  return sym == kEofSymbol ||
         (T.nsections != 0 && T.sectionEnd != nullptr && c.section != 0 &&
          sym == T.sectionEnd[c.section - 1u]);
}

template <const auto& T>
inline bool ll_condition(const LlContext& c, std::uint16_t condition,
                         const Token& token, TextOffset gapFrom) {
  return ll_condition_text<T>(std::string_view(c.src, c.len), condition, token,
                              gapFrom, ll_ends<T>(c, token.sym));
}

// Where the text the descent reads ends: the document's end, or in a section
// the first token that closes it, looked for from `at` up to a token starting
// at `upto`, in the bytes before `clip`. `reached` rises to how far the search
// read, which is `clip` where a walk ran into it.
template <const auto& T>
TextOffset ll_text_end(const LlContext& c, TextOffset at, TextOffset upto,
                       TextOffset& reached, TextOffset clip = ~TextOffset{0}) {
  if constexpr (T.nsections == 0) {
    (void)at, (void)upto, (void)reached, (void)clip;
    return c.len;
  } else {
    if (c.section == 0) return c.len;
    const TextOffset len = std::min(clip, c.len);
    LexFailedWalks<LlLexTape<T>> failed;
    const auto mode = ll_err_mode<T>(c);
    for (TextOffset p = at; p < len && p <= upto;) {
      c.stop.poll();
      TextOffset halt = p;
      Scan s;
      if (!failed.fails(LlLexTape<T>{}, c.src, len, mode, p, halt,
                        [&] { c.stop.poll(); }))
        s = ll_match_text<T>(c.src, len, mode, p, halt);
      reached = std::max({reached, halt, s.end});
      if (s.term == kInvalidSymbol || s.end <= p) {
        ++p;
        continue;
      }
      if (ll_ends<T>(c, s.term)) return p;
      p = s.end;
    }
    return c.len;
  }
}

}  // namespace cursiv
