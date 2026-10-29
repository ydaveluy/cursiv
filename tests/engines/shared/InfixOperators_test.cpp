#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/fuzz/FuzzLane.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/CaretOffers.hpp"
#include "engines/shared/InfixOperatorsGrammar.hpp"
#include "engines/shared/Checks.hpp"
#include "engines/shared/UnaryLevelsGrammar.hpp"
#include "infixoperatorsll/generated/Ll/Module.hpp"
#include "infixoperatorslr/generated/Lr/Module.hpp"
#include "unarylevelsll/generated/Ll/Module.hpp"
#include "unarylevelslr/generated/Lr/Module.hpp"

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
  same_text(engine, text, got, expected);
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
  check(p, "shared:", "a IS EQUAL b", "(a ISEQUAL b)");
  check(p, "shared:", "a IS LIKE b", "(a ISLIKE b)");
  check(p, "shared:", "a IS LIKE b IS EQUAL c", "(a ISLIKE (b ISEQUAL c))");
  check(p, "shared:", "a IS EQUAL b IS LIKE c", "((a ISEQUAL b) ISLIKE c)");
  check(p, "shared:", "a IS LIKE b IS LIKE c", "((a ISLIKE b) ISLIKE c)");
}

void a_sequence_is_one_arm_of_a_choice(const cursiv::Parser& p) {
  check(p, "arm:", "a + b", "(a + b)");
  check(p, "arm:", "a NOT EQUAL b", "(a NOTEQUAL b)");
  check(p, "arm:", "a + b NOT EQUAL c + d", "(((a + b) NOTEQUAL c) + d)");
}

void as_names_the_constant_in_place(const cursiv::Parser& p) {
  check(p, "valued:", "a == b", "(a equal b)");
  check(p, "valued:", "a IS NOT b", "(a NOTEQUAL b)");
  check(p, "valued:", "a IS\n  NOT b", "(a NOTEQUAL b)");
  check(p, "valued:", "a == b IS NOT c", "((a equal b) NOTEQUAL c)");
  check(p, "valued:", "a <> b ~= c", "((a NOTEQUAL b) NOTEQUAL c)");
  check(p, "valued:", "a = b", "<refused>");
  check(p, "valued:", "a IS b", "<refused>");
}

// The constant `as<V>` names reaches a member outside a climb too.
void as_assigns_its_constant(const cursiv::Parser& p) {
  for (const auto& [text, want] : {std::pair{"stated:SAME", Op::equal},
                                   {"stated:NOT SAME", Op::NOTEQUAL},
                                   {"stated:NOT\n  SAME", Op::NOTEQUAL}}) {
    const cursiv::ParseResult r = p.parse(text);
    const auto* const doc = r.value_as<ast::Doc>();
    const auto* const coded =
        doc == nullptr ? nullptr : cursiv::ast_cast<ast::Coded>(doc->value);
    CHECK(r.accepted && r.diagnostics.empty() && coded != nullptr);
    if (coded != nullptr) CHECK(coded->op == want);
  }
  const cursiv::ParseResult r = p.parse("stated:SAME SAME");
  CHECK(!r.accepted || !r.diagnostics.empty());
}

// A called data rule stores the value of the alternative it took, as the
// same alternative written in place does, and a rule that only calls it
// passes that value on.
void a_data_rule_passes_its_alternative_value(const cursiv::Parser& p) {
  for (const auto& [text, want] :
       {std::pair{"visible:called public a", Vis::Public},
        {"visible:called protected a", Vis::Protected},
        {"visible:called private a", Vis::Private},
        {"visible:lone protected a", Vis::Protected},
        {"visible:wordy is protected a", Vis::Protected},
        {"visible:region protected a", Vis::Protected},
        {"visible:regioned protected a", Vis::Protected},
        {"visible:tightenedrepeated protected a", Vis::Protected},
        {"visible:regionedtail protected a", Vis::Protected},
        {"visible:inline public a", Vis::Public},
        {"visible:inline protected a", Vis::Protected},
        {"visible:inline private a", Vis::Private}}) {
    const cursiv::ParseResult r = p.parse(text);
    const auto* const decl = r.value_as<ast::Decl>();
    CHECK(r.accepted && r.diagnostics.empty() && decl != nullptr);
    if (decl != nullptr) CHECK(decl->vis == want && decl->name == "a");
  }
}

// A token the repair drops in the region, after the value, stays in the tree:
// the member still takes the value, and the deletion is the only diagnostic.
void a_dropped_token_leaves_the_passed_value(const cursiv::Parser& p) {
  for (const std::string_view text :
       {"visible:region protected = a", "visible:regioned protected = a"}) {
    const cursiv::ParseResult r = p.parse(text);
    const auto* const decl = r.value_as<ast::Decl>();
    CHECK(r.accepted && r.diagnostics.size() == 1 && decl != nullptr);
    if (r.diagnostics.size() == 1) {
      const auto& d = r.diagnostics.front();
      CHECK(d.kind == cursiv::DiagnosticKind::deleted &&
            text.substr(d.begin, d.end - d.begin) == "=");
    }
    if (decl != nullptr)
      CHECK(decl->vis == Vis::Protected && decl->name == "a");
  }
}

// A condition that ends a region, in a called rule or in place, reads past
// the caller's trivia, as one after the region does, whether it writes or a
// repetition holds it.
void a_condition_ending_a_region_reads_past_the_callers_trivia(
    const cursiv::Parser& p) {
  const auto reads = [&](std::string_view head, const std::string& text) {
    const cursiv::ParseResult r = p.parse(text);
    const auto* const decl = r.value_as<ast::Decl>();
    CHECK(r.accepted && r.diagnostics.empty() && decl != nullptr);
    if (decl != nullptr)
      CHECK(decl->vis == Vis::Protected && decl->name == "a" &&
            decl->seen == head.starts_with("flagged"));
  };
  for (const std::string_view head :
       {"tight", "tightened", "tightenedrepeated", "tightbefore", "flagged",
        "repeated", "flaggedrepeated"}) {
    const std::string start = "visible:" + std::string(head) + " protected";
    for (const std::string_view gap : {" ", "  ", "\n", " \n "})
      reads(head, start + std::string(gap) + "a");
    const cursiv::ParseResult r = p.parse(start + "a");
    CHECK(!r.accepted || !r.diagnostics.empty());
  }
  for (const std::string_view head :
       {"region", "regioned", "tight", "tightened", "tightenedrepeated",
        "tightbefore", "flagged", "repeated", "flaggedrepeated"})
    for (const std::string_view gap : {"#", " # "})
      reads(head, "noted:" + std::string(head) + " protected" +
                      std::string(gap) + "a");
}

// A keyword a repair invents has no row, as a dropped token has none, but the
// state that reads it: the value its alternative names was never written, and
// the member keeps its default.
void an_invented_keyword_leaves_the_member_unwritten(const cursiv::Parser& p) {
  const cursiv::ParseResult r = p.parse("visible:wordy protected a");
  const auto* const decl = r.value_as<ast::Decl>();
  CHECK(r.accepted && r.diagnostics.size() == 1 && decl != nullptr);
  if (r.diagnostics.size() == 1)
    CHECK(r.diagnostics.front().kind == cursiv::DiagnosticKind::inserted);
  if (decl != nullptr) CHECK(decl->vis == Vis::Private && decl->name == "a");
}

// Each level as it is listed, and every node of the climb a `Recoded`.
void binary_builds_the_class_it_names(const cursiv::Parser& p) {
  check(p, "derived:", "a != b", "(a NOTEQUAL b)");
  check(p, "derived:", "a := b := c", "(a equal (b equal c))");
  check(p, "derived:", "a != b := c != d",
        "((a NOTEQUAL b) equal (c NOTEQUAL d))");
  const cursiv::ParseResult r = p.parse("derived:a != b := c");
  const auto* const doc = r.value_as<ast::Doc>();
  const auto* const top =
      doc == nullptr ? nullptr : cursiv::ast_cast<ast::Recoded>(doc->value);
  CHECK(top != nullptr);
  if (top != nullptr) CHECK(cursiv::ast_cast<ast::Recoded>(top->left));
}

void a_recursive_data_rule_spells_an_operator(const cursiv::Parser& p) {
  check(p, "recursive:", "a + b", "(a + b)");
  check(p, "recursive:", "a ! !+ b", "(a !!+ b)");
  check(p, "recursive:", "a - b !+ c - d", "((a - (b !+ c)) - d)");
  check(p, "recursive:", "a ! b", "<refused>");
}

void a_condition_in_an_operator_reads_the_lengthened_token(
    const cursiv::Parser& p) {
  check(p, "lengthened:", "a - b", "(a - b)");
  check(p, "lengthened:", "a - >b", "(a - b)");
  check(p, "lengthened:", "a -> b", "<refused>");
  check(p, "lengthened:", "a * >b", "(a * b)");
  check(p, "lengthened:", "a * > >b", "(a * b)");
  check(p, "lengthened:", "a * >>b", "<refused>");
  check(p, "lengthened:", "a + b - c * d", "(((a + b) - c) * d)");
}

void each_table_reads_its_own_level(const cursiv::Parser& p) {
  check(p, "tables:", "sum: a + b * c + d", "((a + (b * c)) + d)");
  check(p, "tables:", "type: a + b + c", "(a + (b + c))");
  check(p, "tables:", "type: a | b + c", "(a | (b + c))");
  check(p, "tables:", "type: [a | b] + c", "((a | b) + c)");
  check(p, "tables:", "type: @a + b + c", "((a + b) + c)");
  check(p, "tables:", "type: @a * b | c", "((a * b) | c)");
}

std::string_view name_of(Mark mark) {
  static constexpr std::string_view kNames[] = {
      "unset", "power", "slash", "divide", "plus", "minus", "le", "ge",
      "less",  "more",  "MOD",   "REM",    "neg",  "inv",   "bang", "query"};
  return kNames[static_cast<unsigned>(mark)];
}

void spell_marked(const ast::Expr& expr, std::string& out) {
  if (const auto* const name = cursiv::ast_cast<ast::Name>(&expr)) {
    out += name->name;
  } else if (const auto* const pre =
                 cursiv::ast_cast<ast::MarkedPrefix>(&expr)) {
    out += '(';
    out += name_of(pre->op);
    out += ' ';
    spell_marked(*pre->operand, out);
    out += ')';
  } else if (const auto* const post =
                 cursiv::ast_cast<ast::MarkedPostfix>(&expr)) {
    out += '(';
    spell_marked(*post->operand, out);
    out += ' ';
    out += name_of(post->op);
    out += ')';
  } else {
    const auto& marked = static_cast<const ast::Marked&>(expr);
    out += '(';
    spell_marked(*marked.left, out);
    out += ' ';
    out += name_of(marked.op);
    out += ' ';
    spell_marked(*marked.right, out);
    out += ')';
  }
}

// The member written directly, then the expression.
void check_marked(const cursiv::Parser& p, std::string_view text,
                  std::string_view expected) {
  const std::string source = "marking:" + std::string(text);
  const cursiv::ParseResult r = p.parse(source);
  const auto* const doc = r.value_as<ast::MarkedDoc>();
  std::string got = "<refused>";
  if (r.accepted && r.diagnostics.empty() && doc != nullptr &&
      doc->value != nullptr) {
    got = std::string(name_of(doc->direct)) + ' ';
    spell_marked(*doc->value, got);
  }
  same_text(engine, text, got, expected);
}

// Each word of a data rule's alternatives gives its constant to the operator
// it spells, as to the member it is assigned to directly.
void an_operator_takes_the_constant_of_its_alternative(
    const cursiv::Parser& p) {
  check_marked(p, "/; a / b", "slash (a slash b)");
  check_marked(p, "÷; a ÷ b", "divide (a divide b)");
  check_marked(p, "/; a ÷ b / c", "slash ((a divide b) slash c)");
  check_marked(p, "÷; a + b - c", "divide ((a plus b) minus c)");
  check_marked(p, "/; a <= b", "slash (a le b)");
  check_marked(p, "/; a > = b", "slash (a ge b)");
  check_marked(p, "/; a IS LESS b", "slash (a less b)");
  check_marked(p, "/; a IS MORE b", "slash (a more b)");
  check_marked(p, "/; a <= b >= c", "<refused>");
  check_marked(p, "/; ~a", "slash (neg a)");
  check_marked(p, "/; ¬a", "slash (inv a)");
  check_marked(p, "/; a!", "slash (a bang)");
  check_marked(p, "/; a?", "slash (a query)");
  check_marked(p, "÷; ~a! ÷ ¬b?",
               "divide ((neg (a bang)) divide (inv (b query)))");
  check_marked(p, "/; a + b / c IS LESS d - e",
               "slash ((a plus (b slash c)) less (d minus e))");
}

// Operators that name their value themselves keep it: a terminal converting
// its whole body, a keyword read as the enumerator it spells and a literal
// given its constant in place.
void an_operator_naming_its_value_keeps_it(const cursiv::Parser& p) {
  check_marked(p, "/; a ^ b ** c", "slash (a power (b power c))");
  check_marked(p, "/; a MOD b % c", "slash ((a MOD b) REM c)");
  check_marked(p, "÷; a MOD b ^ c / d", "divide (a MOD ((b power c) slash d))");
}

void every_engine(const cursiv::Parser& p) {
  a_sequence_hands_over_its_parts_joined(p);
  trivia_stands_inside_the_sequence(p);
  a_chain_of_the_sequence_groups_by_the_level(p);
  a_sequence_groups_as_its_level_says(p);
  a_terminal_keeps_its_value(p);
  conditions_around_one_element_keep_its_value(p);
  conditions_stand_inside_the_sequence(p);
  adjacency_holds_inside_the_sequence(p);
  a_region_stays_with_its_sequence(p);
  sequences_sharing_a_first_token_are_told_apart(p);
  a_sequence_is_one_arm_of_a_choice(p);
  as_names_the_constant_in_place(p);
  as_assigns_its_constant(p);
  a_data_rule_passes_its_alternative_value(p);
  a_dropped_token_leaves_the_passed_value(p);
  a_condition_ending_a_region_reads_past_the_callers_trivia(p);
  an_invented_keyword_leaves_the_member_unwritten(p);
  binary_builds_the_class_it_names(p);
  a_recursive_data_rule_spells_an_operator(p);
  a_condition_in_an_operator_reads_the_lengthened_token(p);
  each_table_reads_its_own_level(p);
  an_operator_takes_the_constant_of_its_alternative(p);
  an_operator_naming_its_value_keeps_it(p);
}

}  // namespace

void run() {
  engine = "Ll";
  every_engine(*toperators::generated::Ll::create_parser());
  engine = "Lr";
  every_engine(*toperators::generated::Lr::create_parser());
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
  const std::string source = "nonassoc:" + std::string(text);
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
  same_text(engine, text, got, expected);
}

void one_operator_of_the_level_reads(const cursiv::Parser& parser) {
  check(parser, "a <~ b", "(a<~b)");
  check(parser, "a >~ b", "(a>~b)");
  check(parser, "a +~ b <~ c *~ d", "((a+~b)<~(c*~d))");
}

void a_chain_of_the_level_is_refused(const cursiv::Parser& parser) {
  check(parser, "a <~ b <~ c", "<refused>");
  check(parser, "a <~ b >~ c", "<refused>");
  check(parser, "a >~ b >~ c", "<refused>");
  check(parser, "a +~ b <~ c <~ d", "<refused>");
  check(parser, "a <~ b *~ c <~ d", "<refused>");
}

void a_chain_behind_a_looser_operator_is_refused(const cursiv::Parser& parser) {
  check(parser, "a &~ b <~ c <~ d", "<refused>");
  check(parser, "a <~ b &~ c <~ d", "((a<~b)&~(c<~d))");
  check(parser, "a <~ b &~ c &~ d", "(((a<~b)&~c)&~d)");
}

void parentheses_group_the_chain(const cursiv::Parser& parser) {
  check(parser, "(a <~ b) <~ c", "((a<~b)<~c)");
  check(parser, "a <~ (b <~ c)", "(a<~(b<~c))");
}

using Offers = std::vector<std::string>;

void offers(const cursiv::Parser& parser, std::string_view text,
            std::size_t at, const Offers& expected) {
  const std::string source = "nonassoc:" + std::string(text);
  const Offers got = caret_offers::offered(
      parser, source, static_cast<cursiv::TextOffset>(9 + at));
  if (got != expected)
    std::printf("  [%s] caret %zu of %.*s offered a wrong set\n", engine, at,
                static_cast<int>(text.size()), text.data());
  CHECK(got == expected);
}

// Behind an operand that ends a chain of the level, a caret is offered no
// operator of the level: only a looser or a tighter one goes on.
void a_caret_offers_no_chain(const cursiv::Parser& parser) {
  const Offers apart{"&~", "*~", "+~"};
  for (const std::string_view text :
       {"a <~ b", "a <~ b *~ c", "a <~ (b)", "a &~ b <~ c"})
    offers(parser, text, text.size(), apart);
  offers(parser, "a &~ b", 6, {"&~", "*~", "+~", "<~", ">~"});
  offers(parser, "(a <~ b", 7, {"&~", ")", "*~", "+~"});
  offers(parser, "a <~ (b", 7, {"&~", ")", "*~", "+~", "<~", ">~"});
}

// Text a repair dropped behind a token it invented is stepped over: a caret
// in it is offered what the caret before it is.
void a_caret_in_dropped_text_reads_from_before_it(
    const cursiv::Parser& parser) {
  for (const std::size_t at : {0u, 5u, 6u})
    offers(parser, "SAME(~", at, {"(", "ID"});
}

void every_engine(const cursiv::Parser& parser) {
  one_operator_of_the_level_reads(parser);
  a_chain_of_the_level_is_refused(parser);
  a_chain_behind_a_looser_operator_is_refused(parser);
  parentheses_group_the_chain(parser);
  a_caret_offers_no_chain(parser);
  a_caret_in_dropped_text_reads_from_before_it(parser);
}

}  // namespace

void run() {
  engine = "Ll";
  every_engine(*toperators::generated::Ll::create_parser());
  engine = "Lr";
  every_engine(*toperators::generated::Lr::create_parser());
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

void agree(std::string_view prefix,
           std::initializer_list<std::pair<std::string_view, std::string_view>>
               cases) {
  namespace gen = toperators::generated;
  const auto ll = gen::Ll::create_parser();
  const auto llAst = gen::Ll::create_ast_reflection();
  const auto lr = gen::Lr::create_parser();
  const auto lrAst = gen::Lr::create_ast_reflection();
  const auto check = [&](const auto& parser, const auto& ast,
                         std::string_view engine, std::string_view text,
                         std::string_view want) {
    const std::string source = "election:" + std::string(prefix) +
                               std::string(text);
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
  agree("", {{"x", "x"}, {"x y", "ns(x)"}, {"x y y", "ns(xy)"}});
  agree("direct:", {{"a+a+a", "((a+a)+a)"},
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
  agree("multi:", {{"a+a*a", "(a+(a*a))"},
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
  agree("statement:",
        {{"if c then if c then x else x", "[if(if(x,x),_)]"},
         {"if c then if c then x else x else x", "[if(if(x,x),x)]"}});
  agree("loop:",
        {{"if c then if c then x else x", "[if(if(x,x),_)]"},
         {"if c then if c then x else x x", "[if(if(x,x),_),x]"},
         {"x if c then if c then x else x", "[x,if(if(x,x),_)]"},
         {"if c then if c then x else x else x x", "[if(if(x,x),x),x]"}});
}

}  // namespace election_loop_cases

namespace unary_level_cases {

// Prefix and postfix levels in one table, in both engines: groupings, the
// operands a looser prefix may not start, the class each level builds and the
// value its operator writes.

namespace {

using namespace tunary;

std::string_view step_name(Step step) {
  return step == Step::inc ? "++" : step == Step::dec ? "--" : "!";
}

void spell(const ast::Expr& expr, std::string& out) {
  if (const auto* const name = cursiv::ast_cast<ast::Name>(&expr)) {
    out += name->name;
  } else if (const auto* const unary = cursiv::ast_cast<ast::Unary>(&expr)) {
    out += '(';
    out += unary->op;
    spell(*unary->operand, out);
    out += ')';
  } else if (const auto* const no = cursiv::ast_cast<ast::Not>(&expr)) {
    out += '(';
    out += no->word;
    out += ' ';
    spell(*no->body, out);
    out += ')';
  } else if (const auto* const post = cursiv::ast_cast<ast::Postfix>(&expr)) {
    out += '(';
    spell(*post->operand, out);
    out += step_name(post->op);
    out += ')';
  } else if (const auto* const wrapped =
                 cursiv::ast_cast<ast::Wrapped>(&expr)) {
    out += '[';
    spell(*wrapped->value, out);
    out += "?]";
  } else {
    const auto& binary = static_cast<const ast::Binary&>(expr);
    out += '(';
    spell(*binary.left, out);
    out += ' ';
    out += binary.op;
    out += ' ';
    spell(*binary.right, out);
    out += ')';
  }
}

const ast::Expr* value(const cursiv::ParseResult& r) {
  const auto* const doc = r.value_as<ast::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr) return nullptr;
  return doc->value;
}

const char* engine = "";

void check(const cursiv::Parser& parser, std::string_view tag,
           std::string_view text, std::string_view expected) {
  const std::string source = std::string(tag) + std::string(text);
  const cursiv::ParseResult r = parser.parse(source);
  std::string got = "<refused>";
  if (const ast::Expr* const v = value(r); v != nullptr) {
    got.clear();
    spell(*v, got);
  }
  same_text(engine, text, got, expected);
}

void a_prefix_binds_what_is_tighter(const cursiv::Parser& p) {
  check(p, "unary:", "-a * b", "((-a) * b)");
  check(p, "unary:", "a * -b", "(a * (-b))");
  check(p, "unary:", "- -a", "(-(-a))");
  check(p, "unary:", "!-a", "(!(-a))");
  check(p, "unary:", "a - -b", "(a - (-b))");
  check(p, "unary:", "NOT a < b AND c", "((NOT (a < b)) AND c)");
  check(p, "unary:", "NOT NOT a", "(NOT (NOT a))");
  check(p, "unary:", "NOT a OR b", "((NOT a) OR b)");
}

void a_postfix_binds_what_is_tighter(const cursiv::Parser& p) {
  check(p, "unary:", "-a++", "(-(a++))");
  check(p, "unary:", "a++ * b", "((a++) * b)");
  check(p, "unary:", "a * b--", "(a * (b--))");
  check(p, "unary:", "a++ ++", "((a++)++)");
  check(p, "members:", "a & b!", "((a & b)!)");
  check(p, "members:", "~a!", "((~a)!)");
  check(p, "members:", "a!!", "((a!)!)");
}

// An operand starts with no prefix looser than its level, and a tighter
// binary operator takes no looser postfix's result.
void a_looser_operator_is_refused_inside(const cursiv::Parser& p) {
  check(p, "unary:", "a < NOT b", "<refused>");
  check(p, "unary:", "a * NOT b", "<refused>");
  check(p, "unary:", "a < (NOT b)", "(a < (NOT b))");
  check(p, "unary:", "a < b < c", "<refused>");
  check(p, "members:", "a! & b", "<refused>");
  check(p, "members:", "(a!) & b", "((a!) & b)");
}

void a_level_builds_its_own_class(const cursiv::Parser& p) {
  const std::string text = "unary:-a++ * b AND NOT c";
  const cursiv::ParseResult r = p.parse(text);
  const auto* const top = cursiv::ast_cast<ast::Binary>(value(r));
  const auto* const times =
      top == nullptr ? nullptr : cursiv::ast_cast<ast::Binary>(top->left);
  CHECK(times != nullptr && times->op == "*");
  if (times == nullptr) return;
  // The class form builds the class it names, though a base declares the
  // members; the member form the class declaring them.
  const auto* const sign = cursiv::ast_cast<ast::Signed>(times->left);
  CHECK(sign != nullptr && sign->op == "-");
  const auto* const step =
      sign == nullptr ? nullptr : cursiv::ast_cast<ast::Postfix>(sign->operand);
  CHECK(step != nullptr && step->op == Step::inc);
  const auto* const no = cursiv::ast_cast<ast::Not>(top->right);
  CHECK(no != nullptr && no->word == "NOT");
  const cursiv::ParseResult m = p.parse("members:~a!");
  const auto* const bang = cursiv::ast_cast<ast::Postfix>(value(m));
  CHECK(bang != nullptr && bang->op == Step::bang);
  const auto* const tilde =
      bang == nullptr ? nullptr : cursiv::ast_cast<ast::Unary>(bang->operand);
  CHECK(tilde != nullptr && cursiv::ast_cast<ast::Signed>(tilde) == nullptr);
  const cursiv::ParseResult d = p.parse("unary:a--");
  const auto* const dec = cursiv::ast_cast<ast::Postfix>(value(d));
  CHECK(dec != nullptr && dec->op == Step::dec);
}

// A table its primary reaches at its left edge reads the same groupings.
void a_cyclic_table_reads_its_levels(const cursiv::Parser& p) {
  check(p, "cyclic:", "-a * b", "((-a) * b)");
  check(p, "cyclic:", "a * -b", "(a * (-b))");
  check(p, "cyclic:", "-a++", "(-(a++))");
  check(p, "cyclic:", "a++ * b", "((a++) * b)");
  check(p, "cyclic:", "a?", "[a?]");
}

void every_engine(const cursiv::Parser& p) {
  a_prefix_binds_what_is_tighter(p);
  a_postfix_binds_what_is_tighter(p);
  a_looser_operator_is_refused_inside(p);
  a_level_builds_its_own_class(p);
  a_cyclic_table_reads_its_levels(p);
}

}  // namespace

void run() {
  engine = "Ll";
  every_engine(*tunary::generated::Ll::create_parser());
  engine = "Lr";
  every_engine(*tunary::generated::Lr::create_parser());
  // The fuzz lane writes the binary chains of a table with unary levels, and
  // each engine's tree agrees with the one the table states.
  cursiv::fuzzer::lane<Fuzzed>(
      "unary_levels_ll", *tunary::generated::Ll::create_parser(),
      *tunary::generated::Ll::create_ast_reflection(), 200, 0x5a5a5a);
  cursiv::fuzzer::lane<Fuzzed>(
      "unary_levels_lr", *tunary::generated::Lr::create_parser(),
      *tunary::generated::Lr::create_ast_reflection(), 200, 0x6b6b6b);
}

}  // namespace unary_level_cases

int main() {
  cursiv::test::count_expectations();
  infix_operators_cases::run();
  nonassoc_cases::run();
  election_loop_cases::run();
  unary_level_cases::run();
  return cursiv::test::report("InfixOperators_test");
}
