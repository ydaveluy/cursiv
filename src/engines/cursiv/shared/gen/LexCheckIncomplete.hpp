// A lexical check whose walk stopped short of a proof. The grammar stands,
// and the generator warns.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

struct LexCheckIncomplete {
  // The refusal the check stands for, and its two terminals: a section's
  // closer and a terminal, two trivia, or a keyword and a trivia.
  LowerFail check = LowerFail::none;
  std::uint16_t first = 0;
  std::uint16_t second = 0;

  constexpr bool operator==(const LexCheckIncomplete&) const = default;
};

// Each check once, two trivia in either order.
inline void lex_check_incomplete(std::vector<LexCheckIncomplete>& found,
                                 LexCheckIncomplete c) {
  if (c.check == LowerFail::triviaTied && c.second < c.first)
    std::swap(c.first, c.second);
  if (std::ranges::find(found, c) == found.end()) found.push_back(c);
}

// One sentence: the terminals named, the check that could not conclude, and
// what the parser is built on.
template <class Cfg>
std::string lex_check_incomplete_text(const Cfg& g,
                                      const LexCheckIncomplete& c) {
  const auto name = [&](std::uint16_t t) { return quote(g.term_name(t), '`'); };
  const std::string why =
      ": one of them uses something the check cannot follow, such as a "
      "capture, a call, a nested run or a condition -- ";
  if (c.check == LowerFail::sectionEndShared)
    return "terminal " + name(c.first) +
           " closes a `section<>`, but the generator could not check that " +
           name(c.second) + ", a terminal of the embedded grammar, neither " +
           "matches it nor starts with it" + why +
           "the parser is generated as if they never overlap";
  if (c.check == LowerFail::keywordSpellings)
    return "terminal " + name(c.first) +
           " has more than 1 024 keyword spellings, each word counted once " +
           "for each case of its letters, but the generator could not check " +
           "that the trivia " + name(c.second) +
           " never reads on past one of them" + why +
           "the parser is generated as if it never does";
  return "terminals " + name(c.first) + " and " + name(c.second) +
         " are trivia, but the generator could not check that they never " +
         "match the same text with the same length and category" + why +
         "the parser is generated as if they never do";
}

}  // namespace cursiv::engine
