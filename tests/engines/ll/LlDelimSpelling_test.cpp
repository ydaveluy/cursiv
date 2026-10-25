#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/core/parser/Parser.hpp"
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

}  // namespace

int main() {
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
