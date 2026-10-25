#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ExplicitUnitGrammar.hpp"
#include "unitclone/generated/Clone/Module.hpp"
#include "unitrequired/generated/Required/Module.hpp"

int main() {
  namespace g = tllunit::generated;
  const auto required = g::Required::create_parser();
  // A single visible blank ties the skipper and wins. A longer ignored run
  // is one token, so it cannot supply several visible character units.
  for (const std::string_view text : {"x y", "x\ty"}) {
    const auto parsed = required->parse(text);
    CHECK(parsed.accepted && parsed.diagnostics.empty());
    const auto* doc = parsed.value_as<tllunit::Document>();
    CHECK(doc && doc->padding == text.substr(1, 1));
  }
  for (const std::string_view text : {"x   y", "x \t\r\n y", "xy"})
    CHECK(!required->parse(text).diagnostics.empty());
  for (unsigned n = 1; n <= 6; ++n) {
    const std::string text = "[" + std::string(n, ' ') + "]";
    CHECK(!required->parse(text).diagnostics.empty());
  }
  const auto clone = g::Clone::create_parser();
  for (const std::string_view text : {"[ ]xy", "[   ]  x   y", "[\t] x y"}) {
    const auto result = clone->parse(text);
    CHECK(result.accepted && result.diagnostics.empty());
  }
  return cursiv::test::report("LlExplicitUnit_test");
}
