#include <cstdint>
#include <string>
#include <string_view>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/WideChoiceGrammar.hpp"
#include "widechoicell/generated/Ll/Module.hpp"
#include "widechoicell/generated/Ll/Table.hpp"

namespace {

static_assert([]<class... Keywords>(cursiv::Choice<Keywords...>) {
  return sizeof...(Keywords) > 100;
}(twide::Base::Words));
static_assert(twide::generated::Ll::kTable.nafter > 255);

std::string words(const cursiv::ParseResult& r) {
  const auto* d = r.value_as<twide::Doc>();
  if (d == nullptr) return "<none>";
  std::string out;
  for (const auto w : d->words) out += std::string(w) + ' ';
  return out;
}

}  // namespace

int main() {
  const auto ll = twide::generated::Ll::create_parser();
  const auto& table = twide::generated::Ll::kTable;
  for (std::uint16_t t = 0; t < table.nterms; ++t) {
    const auto name = ll->getGrammar().terminalName(t);
    const auto text = ll->getGrammar().terminalText(t);
    CHECK(table.term_name(t) == (name.empty() ? text : name));
    CHECK(table.term_text(t) == text);
  }
  for (const auto& p : {ll.get()}) {
    const auto r =
        p->parse("0 w 1 a05 2 A99 0 w 1 a00 2 A50 0 a99 7 a100 7 wide");
    CHECK(r.accepted && r.diagnostics.empty());
    CHECK(words(r) == "w a05 A99 w a00 A50 a99 a100 wide ");
    CHECK(!p->parse("7 a05").diagnostics.empty());
    CHECK(!p->parse("7 W").diagnostics.empty());
    CHECK(!p->parse("1 a100").diagnostics.empty());
  }
  return cursiv::test::report("LlWideChoice_test");
}
