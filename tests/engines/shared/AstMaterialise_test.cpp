#include "asttreell/generated/Ll/Module.hpp"
#include "asttreelr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/fuzz/CursivLexicon.hpp"
#include "cursiv/fuzz/FuzzLane.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/AstMaterialiseGrammars.hpp"
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace ast_materialise_cases {

namespace {

struct Reader {
  std::unique_ptr<cursiv::Parser> parser;
  std::unique_ptr<cursiv::AstReflection> reflection;
  std::string_view prefix;
};

using Pair = std::array<Reader, 2>;

std::string tree(const Reader& r, const cursiv::ParseResult& result) {
  std::string out;
  if (result.value) cursiv::test::ast_of(*r.reflection, *result.value, out);
  return out;
}

void expect(const Pair& readers, std::string_view text, std::string_view want) {
  for (const Reader& r : readers) {
    const std::string source = "mat:" + std::string(r.prefix) + std::string(text);
    const auto result = r.parser->parse(source);
    CHECK(result.accepted && result.diagnostics.empty() &&
          !result.cst().recovered());
    CHECK(result.error == cursiv::AstBuildError::none);
    const std::string got = tree(r, result);
    if (got != want)
      std::printf("'%.*s'\n  built %s\n  wants %.*s\n",
                  static_cast<int>(text.size()), text.data(), got.c_str(),
                  static_cast<int>(want.size()), want.data());
    CHECK(got == want);
  }
}

template <class G>
void fuzz(const char* name, const Reader& r) {
  cursiv::fuzzer::lane<G>(name, *r.parser, *r.reflection, 200, 0x5eed);
}

}  // namespace

void run() {
  namespace g = ttree::generated;
  cursiv::test::count_expectations();

  const Pair chain{Reader{g::Ll::create_parser(),
                          g::Ll::create_ast_reflection(), "atomchain:"},
                   Reader{g::Lr::create_parser(),
                          g::Lr::create_ast_reflection(), "atomchain:"}};
  expect(chain, "", "Block{}");
  expect(chain, "A", "Block{}");
  expect(chain, "$a", "Block{atoms0=Var{} }");
  expect(chain, "$a.b", "Block{atoms0=Prop{object=Var{} } }");
  expect(chain, "A $a.b[1] B $c",
         "Block{atoms1=Index{object=Prop{object=Var{} } index=Num{} } "
         "atoms3=Var{} }");
  for (const Reader& r : chain) {
    const auto result = r.parser->parse("mat:atomchain:A $a B");
    const auto* block = result.value_as<tmaterialise::Block>();
    CHECK(block != nullptr && block->atoms.size() == 3);
    if (block == nullptr || block->atoms.size() != 3) continue;
    CHECK(std::get<std::string_view>(block->atoms[0]) == "A");
    CHECK(std::get<tmaterialise::Expr*>(block->atoms[1]) != nullptr);
    CHECK(std::get<std::string_view>(block->atoms[2]) == "B");
  }

  const Pair flanked{Reader{g::Ll::create_parser(),
                            g::Ll::create_ast_reflection(), "flanked:"},
                     Reader{g::Lr::create_parser(),
                            g::Lr::create_ast_reflection(), "flanked:"}};
  expect(flanked, "[1]", "Row{}");
  expect(flanked, "[1, 2]", "Row{}");
  expect(flanked, "[\n1,\n[2, 3]\n]", "Row{items1=Row{} }");
  expect(flanked, "[[1], [[2]]]",
         "Row{items0=Row{} items1=Row{items0=Row{} } }");
  for (const Reader& r : flanked) {
    const auto result = r.parser->parse("mat:flanked:[1, [2], 3]");
    const auto* row = result.value_as<tmaterialise::Row>();
    CHECK(row != nullptr && row->items.size() == 3);
    if (row == nullptr || row->items.size() != 3) continue;
    CHECK(std::get<std::int64_t>(row->items[0]) == 1);
    const auto* inner = std::get<tmaterialise::Row*>(row->items[1]);
    CHECK(inner != nullptr && inner->items.size() == 1);
    CHECK(std::get<std::int64_t>(row->items[2]) == 3);
  }

  const Pair shared{Reader{g::Ll::create_parser(),
                           g::Ll::create_ast_reflection(), "sharedprefix:"},
                    Reader{g::Lr::create_parser(),
                           g::Lr::create_ast_reflection(), "sharedprefix:"}};
  expect(shared, "$a.b[1]", "Index{object=Prop{object=Var{} } index=Num{} }");
  expect(shared, "7.x", "Prop{object=Num{} }");
  expect(shared, "-$a.b[1]",
         "Neg{operand=Index{object=Prop{object=Var{} } index=Num{} } }");

  const Pair payload{Reader{g::Ll::create_parser(),
                            g::Ll::create_ast_reflection(), "payloadcreate:"},
                     Reader{g::Lr::create_parser(),
                            g::Lr::create_ast_reflection(), "payloadcreate:"}};
  expect(payload, "a", "Holder{value=Var{} }");
  expect(payload, "a.b", "Holder{value=Prop{object=Var{} } }");
  expect(payload, "a[1].c",
         "Holder{value=Prop{object=Index{object=Var{} index=Num{} } } }");
  expect(payload, "a; b.c; d[2]",
         "Holder{value=Var{} kids0=Prop{object=Var{} } "
         "kids1=Index{object=Var{} index=Num{} } }");

  const Pair group{Reader{g::Ll::create_parser(),
                          g::Ll::create_ast_reflection(), "flaggroup:"},
                   Reader{g::Lr::create_parser(),
                          g::Lr::create_ast_reflection(), "flaggroup:"}};
  expect(group, "", "Holder{}");
  expect(group, "!$a", "Holder{value=Var{} }");
  expect(group, "!$a; $b; $c", "Holder{value=Var{} kids0=Var{} kids1=Var{} }");
  expect(group, "; $b", "Holder{kids0=Var{} }");
  for (const Reader& r : group) {
    const auto flagged = r.parser->parse("mat:flaggroup:!$a");
    const auto* holder = flagged.value_as<tmaterialise::Holder>();
    CHECK(holder != nullptr && holder->flag);
    const auto plain = r.parser->parse("mat:flaggroup:; $b");
    holder = plain.value_as<tmaterialise::Holder>();
    CHECK(holder != nullptr && !holder->flag);
  }

  const Pair ladder{Reader{g::Ll::create_parser(),
                           g::Ll::create_ast_reflection(), "optionalnest:"},
                    Reader{g::Lr::create_parser(),
                           g::Lr::create_ast_reflection(), "optionalnest:"}};
  expect(ladder, "$a", "Var{}");
  expect(ladder, "7", "Num{}");
  expect(ladder, "$a cast as t", "Cast{operand=Var{} }");
  expect(ladder, "$a instance of t", "Inst{operand=Var{} }");
  expect(ladder, "$a cast as t instance of u",
         "Inst{operand=Cast{operand=Var{} } }");
  expect(ladder, "1 + 2", "Bin{left=Num{} right=Num{} }");
  expect(ladder, "1 + $a cast as t + 3",
         "Bin{left=Bin{left=Num{} right=Cast{operand=Var{} } } right=Num{} }");
  expect(ladder, "($a cast as t) + 1",
         "Bin{left=Cast{operand=Var{} } right=Num{} }");
  expect(ladder, "((1))", "Num{}");
  expect(ladder, "-($a + 1)", "Neg{operand=Bin{left=Var{} right=Num{} } }");

  const Pair pass{Reader{g::Ll::create_parser(),
                         g::Ll::create_ast_reflection(), "passthrough:"},
                  Reader{g::Lr::create_parser(),
                         g::Lr::create_ast_reflection(), "passthrough:"}};
  expect(pass, "i", "Var{}");
  expect(pass, "1", "Num{}");
  expect(pass, "(i)", "Var{}");
  expect(pass, "((1))", "Num{}");
  expect(pass, "i=1", "Assign{target=Var{} value=Num{} }");
  expect(pass, "i+1", "Bin{left=Var{} right=Num{} }");
  expect(pass, "i<2", "Bin{left=Var{} right=Num{} }");
  expect(pass, "x=a<b",
         "Assign{target=Var{} value=Bin{left=Var{} right=Var{} } }");

  const Pair relay{
      Reader{g::Ll::create_parser(), g::Ll::create_ast_reflection(), "relay:"},
      Reader{g::Lr::create_parser(), g::Lr::create_ast_reflection(), "relay:"}};
  expect(relay, "!$x", "Neg{operand=Var{} }");
  expect(relay, "!($x+$y)", "Neg{operand=Bin{left=Var{} right=Var{} } }");
  expect(relay, "!!$x", "Neg{operand=Neg{operand=Var{} } }");
  expect(
      relay, "(!$x)+[$a,$b]",
      "Bin{left=Neg{operand=Var{} } right=Pair{first=Var{} second=Var{} } }");
  expect(relay, "[$x,$y]", "Pair{first=Var{} second=Var{} }");

  const Pair terminal{Reader{g::Ll::create_parser(),
                             g::Ll::create_ast_reflection(), "unassignedterminal:"},
                      Reader{g::Lr::create_parser(),
                             g::Lr::create_ast_reflection(), "unassignedterminal:"}};
  expect(terminal, "fn f", "Var{}");
  expect(terminal, "new.target", "Target{}");
  for (const Reader& r : terminal) {
    const auto result = r.parser->parse("mat:unassignedterminal:fn f");
    const auto* var = result.value_as<tmaterialise::Var>();
    CHECK(var != nullptr && var->name == "f");
  }


  const Reader ll{g::Ll::create_parser(), g::Ll::create_ast_reflection(), {}};
  const Reader lr{g::Lr::create_parser(), g::Lr::create_ast_reflection(), {}};
  fuzz<tmaterialise::FuzzSuite>("AstMaterialiseLl", ll);
  fuzz<tmaterialise::FuzzSuite>("AstMaterialiseLr", lr);

}

}  // namespace ast_materialise_cases

namespace nest_adopt_cases {

// A nest behind a called rule wraps the node that rule made, in both engines:
// the wrapper holds it and no empty node stands in its place.

namespace {

struct Engine {
  std::unique_ptr<cursiv::Parser> parser;
  std::unique_ptr<cursiv::AstReflection> reflection;
};

std::string tree(const Engine& e, std::string_view text, std::size_t& nodes) {
  const std::string source = "mat:adopt:" + std::string(text);
  const cursiv::ParseResult r = e.parser->parse(source);
  std::string out;
  nodes = 0;
  if (!r.accepted || !r.diagnostics.empty() || r.value == nullptr)
    return "<refused>";
  nodes = r.nodes.size();
  cursiv::test::ast_of(*e.reflection, *r.value, out);
  return out;
}

void check(const Engine& e, std::string_view text, std::string_view expected,
           std::size_t expectedNodes) {
  std::size_t nodes = 0;
  const std::string got = tree(e, text, nodes);
  if (got != expected)
    std::printf("  %.*s\n    got      %s\n    expected %.*s\n",
                static_cast<int>(text.size()), text.data(), got.c_str(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
  CHECK(nodes == expectedNodes);
}

void a_nest_wraps_the_node_the_called_rule_made(const Engine& e) {
  check(e, "many a", "Ident{}", 1);
  check(e, "many a[b]", "Index{array=Ident{} index=Ident{} }", 3);
  check(e, "one a[b]", "Index{array=Ident{} index=Ident{} }", 3);
  check(e, "some a[b]", "Index{array=Ident{} index=Ident{} }", 3);
  check(e, "some a[b][c]",
        "Index{array=Index{array=Ident{} index=Ident{} } index=Ident{} }", 5);
}

void the_wrapped_node_stays_where_a_member_takes_the_rule(const Engine& e) {
  check(e, "assign a = b", "Assign{target=Ident{} value=Ident{} }", 3);
  check(e, "assign a[b] = c",
        "Assign{target=Index{array=Ident{} index=Ident{} } value=Ident{} }", 5);
}

void a_wrapper_of_another_family_wraps_what_its_member_holds(const Engine& e) {
  check(e, "semi a ;", "Stmt{value=Ident{} }", 2);
}

void every_engine(const Engine& e) {
  a_nest_wraps_the_node_the_called_rule_made(e);
  the_wrapped_node_stays_where_a_member_takes_the_rule(e);
  a_wrapper_of_another_family_wraps_what_its_member_holds(e);
}

}  // namespace

void run() {
  every_engine({ttree::generated::Ll::create_parser(),
                ttree::generated::Ll::create_ast_reflection()});
  every_engine({ttree::generated::Lr::create_parser(),
                ttree::generated::Lr::create_ast_reflection()});

}

}  // namespace nest_adopt_cases

int main() {
  cursiv::test::count_expectations();
  ast_materialise_cases::run();
  nest_adopt_cases::run();
  return cursiv::test::report("AstMaterialise_test");
}
