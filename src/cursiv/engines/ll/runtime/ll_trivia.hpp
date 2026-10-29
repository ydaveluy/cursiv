// The blanks and comments between a token and a caret, read as the lexer reads
// them.
#pragma once

#include <string>
#include <string_view>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/ll/runtime/ll_matcher.hpp"
#include "cursiv/engines/shared/runtime/StampedGrammar.hpp"
#include "cursiv/engines/shared/runtime/Scan.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// Whether the token `mode` reads at `from` still ends at `to` with a word typed
// right behind, as `StampedGrammar::ends_before_word` asks it.
template <const auto& T>
inline bool ll_ends_before_word(std::string_view text, std::uint16_t mode,
                                TextOffset from, TextOffset to, SymbolId term) {
  std::string probe;
  return StampedGrammar::ends_before_word(
      probe, text.substr(from, to - from), term, {T.terms, T.nterms}, T.names,
      [&](std::string_view s) {
        return ll_match_text<T>(s.data(), static_cast<TextOffset>(s.size()),
                                mode, 0);
      });
}

// Whether `mode` reads the text from `from` to `to` as trivia and arrives at
// `to`. `tail` takes the last whole token read, `begin` where it starts.
template <const auto& T>
inline bool ll_trivia_to(std::string_view text, std::uint16_t mode,
                         TextOffset from, TextOffset to, TextOffset& begin,
                         Scan& tail) {
  tail = {};
  while (from < to) {
    const Scan s = ll_match_text<T>(
        text.data(), static_cast<TextOffset>(text.size()), mode, from);
    if (s.term == kInvalidSymbol || s.end <= from || !T.terms[s.term].trivia)
      return false;
    if (s.end > to) return ll_ends_before_word<T>(text, mode, from, to, s.term);
    begin = from;
    tail = s;
    from = s.end;
  }
  return true;
}

}  // namespace cursiv
