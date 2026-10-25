// A record's state holds the position that read it, and past fourteen bits of
// positions the width comes out of the row's: a caret far down a rule that
// long is still offered what the grammar owes there.

#include <cstddef>
#include <string>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/WidePositionsGrammar.hpp"
#include "wideposll/generated/Ll/Module.hpp"
#include "wideposll/generated/Ll/Table.hpp"

namespace {

std::vector<std::string> offered(const cursiv::Parser& parser,
                                 const cursiv::ParseResult& parsed,
                                 std::size_t at) {
  std::vector<cursiv::NextFeature> next;
  const auto where = static_cast<cursiv::TextOffset>(at);
  parser.nextFeaturesAt(parsed.cst(), where, where, next);
  std::vector<std::string> spellings;
  for (const cursiv::NextFeature& f : next)
    spellings.emplace_back(parser.getGrammar().terminalText(f.feature));
  return spellings;
}

static_assert(twidepos::generated::Ll::kTable.stamp.rowBits == 15);

void the_caret_far_down_a_long_rule_is_offered_what_it_owes() {
  const auto parser = twidepos::generated::Ll::create_parser();
  const std::string text =
      std::string(twidepos::kLeading, 'a') + std::string("bc");
  const cursiv::ParseResult parsed = parser->parse(text);
  CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);
  for (const std::size_t at :
       {std::size_t{100}, std::size_t{16383}, std::size_t{16384},
        std::size_t{16400}, twidepos::kLeading - 1}) {
    const auto spellings = offered(*parser, parsed, at);
    CHECK(spellings == std::vector<std::string>{"a"});
  }
  CHECK(offered(*parser, parsed, twidepos::kLeading) ==
        std::vector<std::string>{"b"});
  CHECK(offered(*parser, parsed, twidepos::kLeading + 1) ==
        std::vector<std::string>{"c"});
}

}  // namespace

int main() {
  the_caret_far_down_a_long_rule_is_offered_what_it_owes();
  return cursiv::test::report("LlWidePositions_test");
}
