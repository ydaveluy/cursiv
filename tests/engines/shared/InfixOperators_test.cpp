#include "electionll/generated/Ll/Module.hpp"
#include "electionlr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/InfixOperatorsGrammar.hpp"
#include "infixoperatorsll/generated/Ll/Module.hpp"
#include "infixoperatorslr/generated/Lr/Module.hpp"
#include "infixoperatorssharedll/generated/SharedLl/Module.hpp"
#include "infixoperatorssharedlr/generated/SharedLr/Module.hpp"
#include "nonassocll/generated/Ll/Module.hpp"
#include "nonassoclr/generated/Lr/Module.hpp"
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace infix_operators_cases {

// Operators written in place as sequences, in both engines: each reads as an
// unnamed data rule, with its trivia and its conditions, and hands over its
// parts joined.

namespace {

using namespace tinfixop;

std::string_view name_of(Op op) {
  return op == Op::equal ? "equal" : "NOTEQUAL";
}

void spell(const ast::Expr& expr, std::string& out) {
  if (const auto* const name = cursiv::ast_cast<ast::Name>(&expr)) {
    out += name->name;
    return;
  }
  const ast::Expr* left = nullptr;
  const ast::Expr* right = nullptr;
  std::string_view op;
  if (const auto* const text = cursiv::ast_cast<ast::Text>(&expr)) {
    left = text->left;
    right = text->right;
    op = text->op;
  } else {
    const auto& coded = static_cast<const ast::Coded&>(expr);
    left = coded.left;
    right = coded.right;
    op = name_of(coded.op);
  }
  out += '(';
  spell(*left, out);
  out += ' ';
  out += op;
  out += ' ';
  spell(*right, out);
  out += ')';
}

std::string read(const cursiv::Parser& parser, std::string_view text) {
  const std::string source(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const doc = r.value_as<ast::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr ||
      doc->value == nullptr)
    return "<refused>";
  std::string out;
  spell(*doc->value, out);
  return out;
}

const char* engine = "";

void check(const cursiv::Parser& parser, std::string_view tag,
           std::string_view text,
           std::string_view expected) {
  const std::string got = read(parser, std::string(tag) + std::string(text));
  if (got != expected)
    std::printf("  [%s] %.*s\n    got      %s\n    expected %.*s\n", engine,
                static_cast<int>(text.size()), text.data(), got.c_str(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
}

void a_sequence_hands_over_its_parts_joined(const cursiv::Parser& p) {
  check(p, "joined:", "a NOT EQUAL b", "(a NOTEQUAL b)");
  check(p, "enumerated:", "a NOT EQUAL b", "(a NOTEQUAL b)");
}

void trivia_stands_inside_the_sequence(const cursiv::Parser& p) {
  check(p, "joined:", "a NOT   EQUAL b", "(a NOTEQUAL b)");
  check(p, "joined:", "a NOT\n\tEQUAL b", "(a NOTEQUAL b)");
  check(p, "joined:", "a NOT b", "<refused>");
  check(p, "joined:", "a EQUAL b", "<refused>");
}

void a_chain_of_the_sequence_groups_by_the_level(const cursiv::Parser& p) {
  check(p, "joined:", "a NOT EQUAL b NOT EQUAL c", "((a NOTEQUAL b) NOTEQUAL c)");
  check(p, "joined:", "a NOT EQUAL (b NOT EQUAL c)",
        "(a NOTEQUAL (b NOTEQUAL c))");
}

void a_sequence_groups_as_its_level_says(const cursiv::Parser& p) {
  check(p, "grouped:", "a A B b A B c", "((a AB b) AB c)");
  check(p, "grouped:", "a C D b C D c", "(a CD (b CD c))");
  check(p, "grouped:", "a E F b", "(a EF b)");
  check(p, "grouped:", "a E F b E F c", "<refused>");
  check(p, "grouped:", "a E F b A B c", "(a EF (b AB c))");
}

void a_terminal_keeps_its_value(const cursiv::Parser& p) {
  check(p, "terminal:", "a = b", "(a equal b)");
  check(p, "terminal:", "a = b = c", "((a equal b) equal c)");
}

void conditions_around_one_element_keep_its_value(const cursiv::Parser& p) {
  check(p, "guarded:", "a = (b)", "(a equal b)");
  check(p, "guarded:", "a = b", "<refused>");
  check(p, "lead:", "a = b", "(a equal b)");
  check(p, "lead:", "a\n= b", "<refused>");
}

void conditions_stand_inside_the_sequence(const cursiv::Parser& p) {
  check(p, "between:", "a NOT EQUAL b", "(a NOTEQUAL b)");
  check(p, "between:", "a NOT\nEQUAL b", "<refused>");
}

void adjacency_holds_inside_the_sequence(const cursiv::Parser& p) {
  check(p, "adjacent:", "a >> b", "(a >> b)");
  check(p, "adjacent:", "a > > b", "<refused>");
  check(p, "adjacent:", "a > b", "<refused>");
}

void a_region_stays_with_its_sequence(const cursiv::Parser& p) {
  check(p, "tight:", "a >> b", "(a >> b)");
  check(p, "tight:", "a > > b", "<refused>");
  check(p, "tight:", "a > b", "<refused>");
}

void sequences_sharing_a_first_token_are_told_apart(const cursiv::Parser& p) {
  check(p, "shared:", "a NOT EQUAL b", "(a NOTEQUAL b)");
  check(p, "shared:", "a NOT LIKE b", "(a NOTLIKE b)");
  check(p, "shared:", "a NOT LIKE b NOT EQUAL c", "(a NOTLIKE (b NOTEQUAL c))");
  check(p, "shared:", "a NOT EQUAL b NOT LIKE c", "((a NOTEQUAL b) NOTLIKE c)");
  check(p, "shared:", "a NOT LIKE b NOT LIKE c", "((a NOTLIKE b) NOTLIKE c)");
}

void a_sequence_is_one_arm_of_a_choice(const cursiv::Parser& p) {
  check(p, "arm:", "a + b", "(a + b)");
  check(p, "arm:", "a NOT EQUAL b", "(a NOTEQUAL b)");
  check(p, "arm:", "a + b NOT EQUAL c + d", "(((a + b) NOTEQUAL c) + d)");
}

void every_engine(const cursiv::Parser& p, const cursiv::Parser& shared) {
  a_sequence_hands_over_its_parts_joined(p);
  trivia_stands_inside_the_sequence(p);
  a_chain_of_the_sequence_groups_by_the_level(p);
  a_sequence_groups_as_its_level_says(p);
  a_terminal_keeps_its_value(p);
  conditions_around_one_element_keep_its_value(p);
  conditions_stand_inside_the_sequence(p);
  adjacency_holds_inside_the_sequence(p);
  a_region_stays_with_its_sequence(p);
  sequences_sharing_a_first_token_are_told_apart(shared);
  a_sequence_is_one_arm_of_a_choice(p);
}

}  // namespace

void run() {
  engine = "Ll";
  every_engine(*tinfixop::generated::Ll::create_parser(),
               *tinfixop::generated::SharedLl::create_parser());
  engine = "Lr";
  every_engine(*tinfixop::generated::Lr::create_parser(),
               *tinfixop::generated::SharedLr::create_parser());

}

}  // namespace infix_operators_cases

namespace nonassoc_cases {

// A chain of two operators of a `nonassoc` level is an error in the document,
// in both engines, wherever the chain stands among the other levels.

namespace {

void spell(const tnonassoc::ast::Expr& expr, std::string& out) {
  if (const auto* const atom = cursiv::ast_cast<tnonassoc::ast::Atom>(&expr)) {
    out += atom->name;
    return;
  }
  const auto& binary = static_cast<const tnonassoc::ast::Binary&>(expr);
  out += '(';
  spell(*binary.left, out);
  out += binary.op;
  spell(*binary.right, out);
  out += ')';
}

std::string read(const cursiv::Parser& parser, std::string_view text) {
  const std::string source(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const doc = r.value_as<tnonassoc::ast::Document>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr ||
      doc->value == nullptr)
    return "<refused>";
  std::string out;
  spell(*doc->value, out);
  return out;
}

const char* engine = "";

void check(const cursiv::Parser& parser, std::string_view text,
           std::string_view expected) {
  const std::string got = read(parser, text);
  if (got != expected)
    std::printf("  [%s] %.*s\n    got      %s\n    expected %.*s\n", engine,
                static_cast<int>(text.size()), text.data(), got.c_str(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
}

void one_operator_of_the_level_reads(const cursiv::Parser& parser) {
  check(parser, "a < b", "(a<b)");
  check(parser, "a > b", "(a>b)");
  check(parser, "a + b < c * d", "((a+b)<(c*d))");
}

void a_chain_of_the_level_is_refused(const cursiv::Parser& parser) {
  check(parser, "a < b < c", "<refused>");
  check(parser, "a < b > c", "<refused>");
  check(parser, "a > b > c", "<refused>");
  check(parser, "a + b < c < d", "<refused>");
  check(parser, "a < b * c < d", "<refused>");
}

void a_chain_behind_a_looser_operator_is_refused(const cursiv::Parser& parser) {
  check(parser, "a & b < c < d", "<refused>");
  check(parser, "a < b & c < d", "((a<b)&(c<d))");
  check(parser, "a < b & c & d", "(((a<b)&c)&d)");
}

void parentheses_group_the_chain(const cursiv::Parser& parser) {
  check(parser, "(a < b) < c", "((a<b)<c)");
  check(parser, "a < (b < c)", "(a<(b<c))");
}

void every_engine(const cursiv::Parser& parser) {
  one_operator_of_the_level_reads(parser);
  a_chain_of_the_level_is_refused(parser);
  a_chain_behind_a_looser_operator_is_refused(parser);
  parentheses_group_the_chain(parser);
}

}  // namespace

void run() {
  engine = "Ll";
  every_engine(*tnonassoc::generated::Ll::create_parser());
  engine = "Lr";
  every_engine(*tnonassoc::generated::Lr::create_parser());

}

}  // namespace nonassoc_cases

namespace election_loop_cases {

namespace {

using namespace telectionloop;

std::string expression(const Expression* node) {
  CHECK(node != nullptr);
  if (node == nullptr) return "?";
  if (cursiv::ast_cast<Atom>(node)) return "a";
  if (const auto* n = cursiv::ast_cast<Binary>(node))
    return "(" + expression(n->left) + std::string(n->op) +
           expression(n->right) + ")";
  if (const auto* n = cursiv::ast_cast<Function>(node))
    return "fn(" + expression(n->body) + ")";
  if (const auto* n = cursiv::ast_cast<Include>(node))
    return "include(" + expression(n->body) + ")";
  if (const auto* n = cursiv::ast_cast<Cast>(node))
    return "cast(" + expression(n->body) + ")";
  if (const auto* n = cursiv::ast_cast<Rc>(node)) return n->extra ? "xy" : "x";
  if (const auto* n = cursiv::ast_cast<Ns>(node))
    return "ns(" + expression(n->child) + ")";
  CHECK(false);
  return "?";
}

std::string statement(const Statement* node) {
  if (node == nullptr) return "_";
  if (cursiv::ast_cast<Leaf>(node)) return "x";
  if (const auto* n = cursiv::ast_cast<If>(node))
    return "if(" + statement(n->then) + "," + statement(n->other) + ")";
  CHECK(false);
  return "?";
}

std::string shape(const cursiv::AstNode* root) {
  if (const auto* n = cursiv::ast_cast<ExpressionDocument>(root))
    return expression(n->value);
  if (const auto* n = cursiv::ast_cast<StatementDocument>(root)) {
    std::string out = "[";
    for (const Statement* item : n->items) {
      if (out.size() != 1) out += ',';
      out += statement(item);
    }
    return out + ']';
  }
  CHECK(false);
  return "?";
}

void check_tree(const cursiv::AstReflection& reflection, const cursiv::Cst& cst,
                const cursiv::AstNode& node, const cursiv::AstNode* parent) {
  CHECK(node.parent == parent && node.cstNodeId < cst.size());
  if (node.cstNodeId >= cst.size()) return;
  const auto& span = cst[node.cstNodeId];
  if (parent != nullptr && parent->cstNodeId < cst.size()) {
    const auto& outer = cst[parent->cstNodeId];
    CHECK(outer.begin <= span.begin && span.end <= outer.end);
  }
  reflection.visitMembers(
      node, [&](std::string_view, std::uint32_t, cursiv::AstNode& child) {
        check_tree(reflection, cst, child, &node);
      });
}

template <class Ll, class LlAst, class Lr, class LrAst>
void agree(std::string_view prefix, Ll ll, LlAst llAst, Lr lr, LrAst lrAst,
           std::initializer_list<std::pair<std::string_view, std::string_view>>
               cases) {
  const auto check = [&](const auto& parser, const auto& ast,
                         std::string_view engine, std::string_view text,
                         std::string_view want) {
    const std::string source = std::string(prefix) + std::string(text);
    const auto result = parser->parse(source);
    CHECK(result.accepted && result.diagnostics.empty() && result.value);
    if (!result.value) return;
    const auto got = shape(result.value);
    if (got != want)
      std::cerr << engine << ": " << text << "\nwant: " << want
                << "\n got: " << got << '\n';
    CHECK(got == want);
    check_tree(*ast, result.cst(), *result.value, nullptr);
  };
  for (const auto& [text, want] : cases) {
    check(ll, llAst, "LL", text, want);
    check(lr, lrAst, "LR", text, want);
  }
}

}  // namespace

void run() {
  namespace gen = telectionloop::generated;
  agree("", gen::Ll::create_parser(), gen::Ll::create_ast_reflection(),
        gen::Lr::create_parser(), gen::Lr::create_ast_reflection(),
        {{"x", "x"}, {"x y", "ns(x)"}, {"x y y", "ns(xy)"}});
  agree("direct:", gen::Ll::create_parser(), gen::Ll::create_ast_reflection(),
        gen::Lr::create_parser(), gen::Lr::create_ast_reflection(),
        {{"a+a+a", "((a+a)+a)"},
         {"a+a+a+a", "(((a+a)+a)+a)"},
         {"(a+a)+a", "((a+a)+a)"},
         {"a+(a+a)", "(a+(a+a))"},
         {"fn=>a+a", "fn((a+a))"},
         {"fn=>a+a+a", "fn(((a+a)+a))"},
         {"a+fn=>a+a", "(a+fn((a+a)))"},
         {"include a+a", "include((a+a))"},
         {"a+include a+a", "(a+include((a+a)))"},
         {"cast a+a", "cast((a+a))"},
         {"a+cast a+a", "(a+cast((a+a)))"}});
  agree("multi:", gen::Ll::create_parser(), gen::Ll::create_ast_reflection(),
        gen::Lr::create_parser(), gen::Lr::create_ast_reflection(),
        {{"a+a*a", "(a+(a*a))"},
         {"a*a+a", "((a*a)+a)"},
         {"a*a*a*a", "(((a*a)*a)*a)"},
         {"a+a+a+a", "(((a+a)+a)+a)"},
         {"(a+a)*a", "((a+a)*a)"},
         {"a*(a+a)", "(a*(a+a))"},
         {"fn=>a*a+a", "fn(((a*a)+a))"},
         {"a*fn=>a+a", "(a*fn((a+a)))"},
         {"include a*a+a", "include(((a*a)+a))"},
         {"a*include a+a", "(a*include((a+a)))"},
         {"cast a*a+a", "cast(((a*a)+a))"},
         {"a*cast a+a", "(a*cast((a+a)))"}});
  agree("infix:", gen::Ll::create_parser(), gen::Ll::create_ast_reflection(),
        gen::Lr::create_parser(), gen::Lr::create_ast_reflection(),
        {{"a+a+a", "((a+a)+a)"},
         {"a+a+a+a", "(((a+a)+a)+a)"},
         {"a+a*a", "(a+(a*a))"},
         {"a*a+a", "((a*a)+a)"},
         {"fn=>a*a+a", "fn(((a*a)+a))"},
         {"a*fn=>a+a", "(a*fn((a+a)))"},
         {"include a*a+a", "include(((a*a)+a))"},
         {"a*include a+a", "(a*include((a+a)))"},
         {"cast a*a+a", "cast(((a*a)+a))"},
         {"a*cast a+a", "(a*cast((a+a)))"}});
  agree("statement:", gen::Ll::create_parser(),
        gen::Ll::create_ast_reflection(),
        gen::Lr::create_parser(),
        gen::Lr::create_ast_reflection(),
        {{"if c then if c then x else x", "[if(if(x,x),_)]"},
         {"if c then if c then x else x else x", "[if(if(x,x),x)]"}});
  agree("loop:", gen::Ll::create_parser(),
        gen::Ll::create_ast_reflection(),
        gen::Lr::create_parser(),
        gen::Lr::create_ast_reflection(),
        {{"if c then if c then x else x", "[if(if(x,x),_)]"},
         {"if c then if c then x else x x", "[if(if(x,x),_),x]"},
         {"x if c then if c then x else x", "[x,if(if(x,x),_)]"},
         {"if c then if c then x else x else x x", "[if(if(x,x),x),x]"}});

}

}  // namespace election_loop_cases

int main() {
  cursiv::test::count_expectations();
  infix_operators_cases::run();
  nonassoc_cases::run();
  election_loop_cases::run();
  return cursiv::test::report("InfixOperators_test");
}
