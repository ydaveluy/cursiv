#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "dedentrunflatll/generated/FlatLl/Module.hpp"
#include "engines/ll/DedentRunGrammar.hpp"

#include "dedentrunll/generated/Ll/Module.hpp"

namespace {

struct Reading {
  bool accepted = false;
  std::size_t diagnostics = 0;
  std::string ast;
};

template <class Pair>
Reading read(Pair& p, std::string_view text) {
  const auto result = p.first->parse(text);
  Reading out{result.accepted, result.diagnostics.size(), {}};
  if (result.value) cursiv::test::ast_of(*p.second, *result.value, out.ast);
  return out;
}

template <class Pair>
void expect_closures(Pair& ll, std::string_view text, std::size_t closures,
           bool clean) {
  const auto a = read(ll, text);
  CHECK((a.diagnostics == 0) == clean);
  if (!clean) return;
  std::string want = "Node{";
  for (std::size_t i = 0; i < closures; ++i)
    want += "closures" + std::to_string(i) + "=Closure{} ";
  want += '}';
  CHECK(a.accepted && a.ast == want);
}

template <class Parser>
void closes(const Parser& parser, std::string_view text, std::size_t levels) {
  const auto result = parser->parse(text);
  CHECK(result.accepted && result.diagnostics.empty());
  CHECK(result.template value_as<tlldedentrun::Marks>()->dedents.size() ==
        levels);
}

// A dropped dedent closes a level and owns no byte: the text past it is still
// dropped, whether the entry stops at `eof` or before the text does.
template <class Parser>
void drops_past_dedents_to_the_end(const Parser& parser,
                                   std::string_view text) {
  const auto result = parser->parse(text);
  const auto& cst = result.cst();
  cursiv::TextOffset end = 0;
  for (cursiv::NodeId i = 0; i < cst.size(); ++i)
    end = std::max(end, cst[i].end);
  CHECK(!result.diagnostics.empty() && end == text.size());
}

}  // namespace

int main() {
  namespace g = tlldedentrun::generated;
  auto ll = std::pair{g::Ll::create_parser(), g::Ll::create_ast_reflection()};
  expect_closures(ll, "some:a\n b\n  c\nd", 2, true);
  expect_closures(ll, "some:a\n b\n  c\n d", 1, true);
  expect_closures(ll, "many:a\n b\n  c\nd", 2, true);
  expect_closures(ll, "many:a\n b\n  c\n d", 1, true);
  expect_closures(ll, "many:a\n b\n  c\n  d", 0, true);
  expect_closures(ll, "some:a\n b\n  c\n  d", 0, false);
  drops_past_dedents_to_the_end(ll.first, "many:a\n b\n  c\n  d\n e\nf");
  drops_past_dedents_to_the_end(ll.first, "tail:a\n b\n  c\nd");
  const auto flatLl = g::FlatLl::create_parser();
  for (const auto text : {"a:\n  b:\n    c", "a:\n  b:\n    c\n"}) {
    closes(flatLl, text, 2);
  }
  return cursiv::test::report("LlDedentRun_test");
}
