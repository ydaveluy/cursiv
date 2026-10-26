#include "lextiell/generated/Ll/Module.hpp"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/runtime/LlContext.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "unitsll/generated/Ll/Module.hpp"
#include "unitsll/generated/Ll/Table.hpp"
#include "lextiereservedll/generated/ReservedLl/Module.hpp"
#include "lextiereservedll/generated/ReservedLl/Table.hpp"

namespace {

template <class Parser, class Reflection>
std::string read(Parser& p, Reflection& r, std::string_view text) {
  const auto result = p->parse(text);
  CHECK(result.accepted && result.diagnostics.empty());
  if (!result.value) return "";
  std::string out;
  cursiv::test::ast_of(*r, *result.value, out);
  return out;
}

// Every reading has an independently specified AST.
template <class Ll, class LlAst>
void expect_cases(Ll ll, LlAst llr,
           std::initializer_list<std::pair<std::string_view, std::string_view>>
               cases, std::string_view prefix = {}) {
  for (const auto& [text, want] : cases) {
    CHECK(read(ll, llr, std::string(prefix) + std::string(text)) == want);
  }
}

// A keyword reserving the spelling two names share leaves its own modes one
// token there: no choice read under them waits on a lexical tie.
void a_reserved_spelling_ties_no_mode_that_reserves_it() {
  constexpr auto& T = tlllextie::generated::ReservedLl::kTable;
  std::uint16_t keyword = 0;
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.term_name(t) == "x") keyword = t;
  CHECK(keyword != 0);
  for (std::uint16_t m = 0; m < T.nmodes; ++m)
    if (T.mode_admits(m, keyword)) CHECK(T.modeMulti[m] == 0);
}

// The token of a lookahead across a blank is the blank when a visible token
// reads it at equal length.
void a_visible_token_beats_the_blank_in_a_lookahead() {
  constexpr auto& T = tllunit::generated::Ll::kTable;
  cursiv::Cst tree(" a");
  cursiv::CstBuilder builder(tree);
  builder.bind_stream();
  cursiv::LlContext context(tree.text(), builder);
  cursiv::LlMask<T.termWords> viable;
  for (std::uint16_t t = 1; t < T.nterms; ++t)
    if (T.term_name(t) == "GAP") viable.set(t);
  std::vector<std::uint16_t> readings;
  cursiv::TextOffset from = 0;
  CHECK(cursiv::ll_look<T>(context, from, viable, readings, T.errMode));
  CHECK(from == 1 && readings.size() == 1);
}

}  // namespace

int main() {
  a_visible_token_beats_the_blank_in_a_lookahead();
  a_reserved_spelling_ties_no_mode_that_reserves_it();
  using namespace tlllextie::generated;
  constexpr std::string_view number = "Doc{value=Number{} }";
  constexpr std::string_view name = "Doc{value=Name{} }";

  expect_cases(Ll::create_parser(), Ll::create_ast_reflection(),
        {{"1;", number},
         {"1;!", "Doc{value=Bang{} }"},
         {"@1;", name},
         {"1.5;", number}}, "cell:");
  constexpr std::string_view ctor = "Class{members0=Ctor{} }";
  constexpr std::string_view method = "Class{members0=Method{} }";
  expect_cases(Ll::create_parser(), Ll::create_ast_reflection(),
        {{"class Chest { Chest(); }", ctor},
         {"class Chest { @a Chest(); }", ctor},
         {"class Chest { const Chest(); }", ctor},
         {"class Chest { Box size(); }", method},
         {"class Chest { get(); }", method}}, "ctor:");
  expect_cases(Ll::create_parser(), Ll::create_ast_reflection(),
        {{"class Chest { Chest(); }", method},
         {"class Chest { @a Chest(); }", method},
         {"class Chest { const Chest(); }", ctor},
         {"class Chest { Box size(); }", method},
         {"class Chest { get(); }", method}}, "method:");
  expect_cases(ReservedLl::create_parser(), ReservedLl::create_ast_reflection(),
        {{"x;", "Doc{value=Bang{} }"},
         {"aa:", name},
         {"bb,", number},
         {"# x !", name},
         {"# x ?", number}});
  constexpr std::string_view numberTerm =
      "Query{clauses0=Fuzzed{primary=Number{} } }";
  constexpr std::string_view wordTerm =
      "Query{clauses0=Fuzzed{primary=Name{} } }";
  expect_cases(Ll::create_parser(), Ll::create_ast_reflection(),
        {{"3", numberTerm},
         {"3.5", numberTerm},
         {"3m", wordTerm},
         {"3.", wordTerm},
         {"3:x", "Query{clauses0=Field{value=Fuzzed{primary=Name{} } } }"},
         {"a:3", "Query{clauses0=Field{value=Fuzzed{primary=Number{} } } }"},
         {"3<4", "Query{clauses0=Compare{value=Name{} } }"},
         {"a~3",
          "Query{clauses0=Fuzzed{primary=Name{} "
          "fuzzy0=Fuzzy{similarity=Number{} } } }"},
         {"a~ 3 ~",
          "Query{clauses0=Fuzzed{primary=Name{} "
          "fuzzy0=Fuzzy{similarity=Number{} } fuzzy1=Fuzzy{} } }"},
         {"3 3",
          "Query{clauses0=Fuzzed{primary=Number{} } "
          "clauses1=Fuzzed{primary=Number{} } }"},
         {"a 3 b",
          "Query{clauses0=Fuzzed{primary=Name{} } "
          "clauses1=Fuzzed{primary=Number{} } "
          "clauses2=Fuzzed{primary=Name{} } }"}}, "clauses:");

  expect_cases(tllunit::generated::Ll::create_parser(),
        tllunit::generated::Ll::create_ast_reflection(),
        {{"a b", "Stmts{stmts0=Name{} stmts1=Name{} stmts2=Name{} }"}}, "blank:");
  return cursiv::test::report("LlLexTie_test");
}
