#include "indirectfoldll/generated/Ll/Module.hpp"
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/OperatorConditionGrammar.hpp"

namespace operator_condition_cases {
namespace {

namespace g = tllexpressions::generated;

struct Reader {
  const char* engine;
  std::unique_ptr<cursiv::Parser> parser;
  std::unique_ptr<cursiv::AstReflection> reflection;
  std::string_view prefix;
};

struct Case {
  std::string_view text;
  std::string_view tree;
};

constexpr std::string_view kOne =
    "Document{value=Binary{left=Number{} right=Number{} } }";
constexpr std::string_view kTwo =
    "Document{value=Binary{left=Binary{left=Number{} right=Number{} } "
    "right=Number{} } }";
constexpr std::string_view kRight =
    "Document{value=Binary{left=Number{} right=Binary{left=Number{} "
    "right=Number{} } } }";
constexpr std::string_view kJoined =
    "Sequence{items0=Binary{left=Number{} right=Number{} } }";
constexpr std::string_view kApart =
    "Sequence{items0=Number{} items1=Unary{operand=Number{} } }";

void read(const Reader& r, const Case& c) {
  const std::string source = std::string(r.prefix) + std::string(c.text);
  const auto result = r.parser->parse(source);
  const bool clean = result.accepted && result.diagnostics.empty() &&
                     !result.cst().recovered() && result.value != nullptr;
  std::string tree;
  if (clean) cursiv::test::ast_of(*r.reflection, *result.value, tree);
  const bool wants = !c.tree.empty();
  if (clean != wants || (wants && tree != c.tree))
    std::printf("%s: '%.*s' read %s%s, wants %s\n", r.engine,
                static_cast<int>(c.text.size()), c.text.data(),
                clean ? "clean " : "with diagnostics", tree.c_str(),
                wants ? std::string(c.tree).c_str() : "diagnostics");
  CHECK(clean == wants);
  if (wants) CHECK(tree == c.tree);
  CHECK(wants || !result.diagnostics.empty());
}

template <std::size_t N>
void expect_cases(const Reader (&readers)[N],
                       std::span<const Case> cases) {
  for (const Reader& r : readers)
    for (const Case& c : cases) read(r, c);
}

void a_condition_after_the_operator_reads_the_gap_behind_it() {
  const Reader readers[] = {
      {"LL", g::Ll::create_parser(), g::Ll::create_ast_reflection(), "after:"}};
  const Case cases[] = {
      {"1", "Document{value=Number{} }"},
      {"1 + 2", kOne},
      {"1\n+ 2", kOne},
      {"1 + 2 + 3", kTwo},
      {"1\n+ 2\n+ 3", kTwo},
      {"1 +\n2", ""},
      {"1 +\r\n2", ""},
      {"1 + 2 +\n3", ""},
      {"1\n+\n2", ""},
  };
  expect_cases(readers, cases);
}

void conditions_on_both_sides_of_the_operator_each_read_their_own_gap() {
  const Reader readers[] = {
      {"LL", g::Ll::create_parser(), g::Ll::create_ast_reflection(), "both:"}};
  const Case cases[] = {
      {"1 +\n2", kOne}, {"1 +\n2 +\n3", kTwo}, {"1 + 2", ""},
      {"1\n+\n2", ""},  {"1\n+ 2", ""},        {"1 +\n2 + 3", ""},
  };
  expect_cases(readers, cases);
}

void a_guarded_operator_among_plain_ones_reads_its_own_gap() {
  const Reader readers[] = {
      {"LL", g::Ll::create_parser(), g::Ll::create_ast_reflection(), "mixed:"}};
  const Case cases[] = {
      {"1 * 2 + 3", kTwo},   {"1\n* 2 + 3", kTwo},    {"1 * 2\n+ 3", kTwo},
      {"1 + 2 * 3", kRight}, {"1\n+ 2\n* 3", kRight}, {"1 +\n2 * 3", ""},
      {"1 * 2 +\n3", ""},
  };
  expect_cases(readers, cases);
}

// The operator's token also opens an item of its own: a break behind it parts
// the operands, and the same text reads as two items.
void a_break_behind_the_operator_parts_what_it_would_join() {
  const Reader readers[] = {
      {"LL", g::Ll::create_parser(), g::Ll::create_ast_reflection(), "list:"}};
  const Case cases[] = {
      {"1 + 2", kJoined},
      {"1\n+ 2", kJoined},
      {"1 +\n2", kApart},
      {"1 + \n 2", kApart},
  };
  expect_cases(readers, cases);
}

void the_operator_text_leaves_the_condition_out() {
  const auto ll = g::Ll::create_parser();
  for (const cursiv::Parser* parser : {ll.get()}) {
    const auto result = parser->parse("after:1\n+ 2");
    const auto* document = result.value_as<toperatorcondition::Document>();
    const auto* sum =
        document == nullptr
            ? nullptr
            : cursiv::ast_cast<toperatorcondition::Binary>(document->value);
    CHECK(sum != nullptr && sum->op == "+");
  }
}

}  // namespace

void run() {
  a_condition_after_the_operator_reads_the_gap_behind_it();
  conditions_on_both_sides_of_the_operator_each_read_their_own_gap();
  a_guarded_operator_among_plain_ones_reads_its_own_gap();
  a_break_behind_the_operator_parts_what_it_would_join();
  the_operator_text_leaves_the_condition_out();
}

}  // namespace operator_condition_cases
