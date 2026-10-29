// A lexical check whose walk stopped short of a proof: it found an overlap
// only where an assertion it does not read holds, or none on the texts it
// read while some text ran on where it does not follow. The grammar stands,
// and the generator warns.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"

namespace cursiv {

struct LexCheckIncomplete {
  // The refusal the check stands for: `sectionEndShared`, where `first`
  // closes a section and `second` is a terminal its grammar reads, or
  // `triviaTied`, for two trivia terminals.
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
  const auto name = [&](std::uint16_t t) {
    return '`' + std::string(g.term_name(t)) + '`';
  };
  const std::string why =
      ": one of them uses something the check cannot follow, such as a "
      "capture, a call, a nested run or a condition -- ";
  if (c.check == LowerFail::sectionEndShared)
    return "terminal " + name(c.first) +
           " closes a `section<>`, but the generator could not check that " +
           name(c.second) + ", a terminal of the embedded grammar, neither " +
           "matches it nor starts with it" + why +
           "the parser is generated as if they never overlap";
  return "terminals " + name(c.first) + " and " + name(c.second) +
         " are trivia, but the generator could not check that they never " +
         "match the same text with the same length and category" + why +
         "the parser is generated as if they never do";
}

}  // namespace cursiv
