// The grammar's keywords, written into `repository.keywords.match` of an
// editor's TextMate file; the rest of that file is written by hand.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cursiv::engine {

// A word a grammar spells, and whether it ignores letter case.
struct TextMateKeyword {
  std::string_view text;
  bool folded = false;
};

// The text of every token a rule reads as one literal or keyword. Trivia and
// the words only a condition reads are no tokens a reader sees.
template <class Cfg>
std::vector<TextMateKeyword> textmate_keywords(const Cfg& c) {
  std::vector<TextMateKeyword> out;
  for (std::uint16_t t = 0; t < c.nterms; ++t)
    if (c.terms[t].spelled && !c.terms[t].trivia &&
        !c.termBuild[t].predicateOnly)
      out.push_back({c.term_text(t), c.termBuild[t].folded});
  return out;
}

// The pattern that matches the `keywords` that hold an ASCII letter as whole
// words, longest first, each in any case where it ignores letter case. With
// no such word, it matches nothing.
std::string textmate_keyword_match(std::vector<TextMateKeyword> keywords);

// `json` with its string at `repository.keywords.match` replaced by `match`,
// every other byte unchanged. Nothing where it has no such string or its
// brackets do not match; the rest of the JSON is not checked.
std::optional<std::string> with_keyword_match(std::string_view json,
                                              std::string_view match);

}  // namespace cursiv::engine
