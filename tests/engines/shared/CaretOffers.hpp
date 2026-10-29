// What a caret is offered, as a test compares it across engines: each
// terminal by its one spelling, or else by its name, sorted.
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"

namespace caret_offers {

inline std::vector<std::string> offered(const cursiv::Parser& parser,
                                        std::string_view source,
                                        cursiv::TextOffset at) {
  const cursiv::ParseResult parsed = parser.parse(source);
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(parsed.cst(), at, at, next);
  std::vector<std::string> out;
  for (const cursiv::NextFeature& f : next) {
    const auto spellings = parser.getGrammar().terminalSpellings(f.feature);
    out.emplace_back(spellings.size() == 1
                         ? spellings.front()
                         : parser.getGrammar().terminalName(f.feature));
  }
  std::ranges::sort(out);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace caret_offers
