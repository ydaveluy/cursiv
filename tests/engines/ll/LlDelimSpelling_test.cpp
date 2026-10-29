#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/gen/LlLowering.hpp"
#include "cursiv/engines/lr/gen/LrLowering.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "delimll/generated/Ll/Module.hpp"
#include "engines/ll/DelimSpellingGrammar.hpp"

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

template <class... Pairs>
void agree(std::string_view text, std::string_view expected, Pairs&... pairs) {
  const Reading readings[] = {read(pairs, text)...};
  std::size_t at = 0;
  for (const auto& r : readings) {
    const bool same =
        r.accepted && r.diagnostics == 0 && !r.ast.empty() &&
        r.ast == expected;
    if (!same)
      std::printf("delim spelling reading %zu (%zu diagnostics): %s\n", at,
                  r.diagnostics, std::string(text).c_str());
    CHECK(same);
    ++at;
  }
}

// The brackets are the terminals the rules read: a spelling read only through
// a named terminal is that terminal, and no terminal of its own.
template <class Lowered>
void brackets_are_terminals_read(const Lowered& low) {
  unsigned opens = 0, closes = 0;
  for (std::uint16_t t = 1; t < low.cfg.nterms; ++t) {
    const std::string_view name = low.cfg.term_name(t);
    for (const std::string_view named : {"[", "]", "{", "}", ">"})
      CHECK(name != named);
    opens += low.cfg.terms[t].delim > 0 ? 1 : 0;
    closes += low.cfg.terms[t].delim < 0 ? 1 : 0;
  }
  CHECK(opens == 4 && closes == 4);
}

}  // namespace

int main() {
  brackets_are_terminals_read(cursiv::LlLowering<tdelim::Ll>::lower());
  brackets_are_terminals_read(cursiv::LrLowering<tdelim::Lr>::lower());
  namespace g = tdelim::generated;
  auto ll = std::pair{g::Ll::create_parser(), g::Ll::create_ast_reflection()};
  struct Case { std::string_view before, body, after; };
  for (const auto& c : {
           Case{"a ", "b\n c", "\nd\n"},
           Case{"a ", "\n  b\n  .c\n", "\nd\n"},
           Case{"s:\n  a ", "\nb\n   .c\n  ", "\n  d\n"},
           Case{"s:\n  a ", " b\n .c d\n.e", "\ne\n"},
       }) {
    const auto wrap = [&](std::string_view open, std::string_view close) {
      return std::string(c.before) + std::string(open) + std::string(c.body) +
             std::string(close) + std::string(c.after);
    };
    const auto expected = read(ll, wrap("(", ")"));
    for (const auto [open, close] :
         {std::pair{"(", ")"}, std::pair{"[", "]"}, std::pair{"{", "}"},
          std::pair{"<", ">"}, std::pair{"<~", ">"}})
      agree(wrap(open, close), expected.ast, ll);
  }
  return cursiv::test::report("LlDelimSpelling_test");
}
