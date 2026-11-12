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

namespace cursiv::engine {

// The mode the text of `section` (as `LlContext::section` names it) is read
// in, whole: every terminal of its grammar and the token that closes it.
template <const auto& T>
constexpr std::uint16_t ll_section_mode(std::uint8_t section) {
  return T.sectionErr[section - 1u];
}

// The mode a fault is lexed with at a point reading `mode`: the terminals of
// its configuration. With no mode in hand, those of `section`, or of the
// document's own configuration.
template <const auto& T>
constexpr std::uint16_t ll_err_mode(std::uint16_t mode,
                                    [[maybe_unused]] std::uint8_t section) {
  if constexpr (T.errModeOf == nullptr) {
    return T.errMode;
  } else {
    if (mode < T.nmodes) return T.errModeOf[mode];
    if constexpr (T.nsections != 0)
      if (section != 0) return ll_section_mode<T>(section);
    return T.errModeOf[T.tailMode];
  }
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
// the first closing token, from `at` up to a token starting at `upto`, before
// `clip`. `reached` rises to how far the search read.
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
    const auto mode = ll_section_mode<T>(c.section);
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

}  // namespace cursiv::engine
