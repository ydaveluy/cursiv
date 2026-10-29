#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ExplicitUnitAst.hpp"
#include "unitsll/generated/Ll/Module.hpp"

int main() {
  namespace g = tllunit::generated;
  const auto parser = g::Ll::create_parser();
  // A single visible blank ties the skipper and wins. A longer ignored run
  // is one token, so it cannot supply several visible character units.
  for (const std::string_view text : {"x y", "x\ty"}) {
    const std::string source = "required:" + std::string(text);
    const auto parsed = parser->parse(source);
    CHECK(parsed.accepted && parsed.diagnostics.empty());
    const auto* doc = parsed.value_as<tllunit::Document>();
    CHECK(doc && doc->padding == text.substr(1, 1));
  }
  for (const std::string_view text : {"x   y", "x \t\r\n y", "xy"})
    CHECK(!parser->parse("required:" + std::string(text)).diagnostics.empty());
  for (unsigned n = 1; n <= 6; ++n) {
    const std::string text = "[" + std::string(n, ' ') + "]";
    CHECK(!parser->parse("required:" + text).diagnostics.empty());
  }
  for (const std::string_view text : {"[ ]xy", "[   ]  x   y", "[\t] x y"}) {
    const std::string source = "clone:" + std::string(text);
    const auto result = parser->parse(source);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* doc = result.value_as<tllunit::Document>();
    CHECK(doc && doc->padding == text.substr(1, text.find(']') - 1));
  }
  return cursiv::test::report("LlExplicitUnit_test");
}
