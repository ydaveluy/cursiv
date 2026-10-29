#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "asttreell/generated/Ll/Module.hpp"
#include "asttreelr/generated/Lr/Module.hpp"
#include "engines/shared/DirectiveSequenceGrammar.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace directive_sequence_cases {

// A sequence of several elements that read, under `assign` or `append`, is an
// unnamed data rule in both engines: the member takes its parts joined, and
// one element with conditions around it keeps its own value.

namespace {

using namespace tdirseq;

const char* engine = "";

std::string read(const cursiv::Parser& parser, std::string_view text,
                 std::string (*show)(const ast::Doc&)) {
  const std::string source = "dir:" + std::string(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const doc = r.value_as<ast::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr)
    return "<refused>";
  return show(*doc);
}

void check(const cursiv::Parser& parser, std::string_view tag,
           std::string_view text,
           std::string (*show)(const ast::Doc&), std::string_view expected) {
  const std::string got = read(parser, std::string(tag) + std::string(text), show);
  if (got != expected)
    std::printf("  [%s] %.*s\n    got      %s\n    expected %.*s\n", engine,
                static_cast<int>(text.size()), text.data(), got.c_str(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
}

std::string name_of(const ast::Doc& doc) { return std::string(doc.name); }
std::string text_of(const ast::Doc& doc) { return std::string(doc.text); }
std::string op_of(const ast::Doc& doc) {
  return doc.op == Op::equal ? "equal" : "NOTEQUAL";
}
std::string box_of(const ast::Doc& doc) {
  if (doc.box == nullptr) return "<no box>";
  std::string out = "box:";
  for (const auto item : doc.box->items) {
    out += ' ';
    out += item;
  }
  return out;
}
std::string names_of(const ast::Doc& doc) {
  std::string out;
  for (const auto name : doc.names) {
    if (!out.empty()) out += ',';
    out += name;
  }
  return out;
}

void a_sequence_stores_its_parts_joined(const cursiv::Parser& p) {
  check(p, "dotted:", "a", name_of, "a");
  check(p, "dotted:", "a.b.c", name_of, "a.b.c");
}

void trivia_is_left_out(const cursiv::Parser& p) {
  check(p, "dotted:", "a . b . c", name_of, "a.b.c");
  check(p, "dotted:", "a\n.\tb", name_of, "a.b");
  check(p, "dotted:", "a . ", name_of, "<refused>");
}

void an_enumeration_takes_the_enumerator_of_that_name(const cursiv::Parser& p) {
  check(p, "enumerated:", "NOT EQUAL", op_of, "NOTEQUAL");
  check(p, "enumerated:", "NOT   EQUAL", op_of, "NOTEQUAL");
  check(p, "enumerated:", "NOT", op_of, "<refused>");
  check(p, "enumerated:", "EQUAL", op_of, "<refused>");
}

void a_text_member_takes_the_joined_spelling(const cursiv::Parser& p) {
  check(p, "spelled:", "NOT EQUAL", text_of, "NOTEQUAL");
  check(p, "spelled:", "NOT\nEQUAL", text_of, "NOTEQUAL");
}

void append_adds_one_joined_element_per_turn(const cursiv::Parser& p) {
  check(p, "listed:", "a:b c:d", names_of, "a:b,c:d");
  check(p, "listed:", "a : b", names_of, "a:b");
  check(p, "listed:", "a:b c", names_of, "<refused>");
}

void a_data_rule_call_is_one_part(const cursiv::Parser& p) {
  check(p, "called:", "a ! b", text_of, "a!B");
  check(p, "called:", "a!b", text_of, "a!B");
}

void conditions_around_one_element_keep_its_value(const cursiv::Parser& p) {
  check(p, "guarded:", "!= (", op_of, "NOTEQUAL");
  check(p, "guarded:", "!= x", op_of, "<refused>");
}

void an_operand_that_builds_a_node_gives_that_node(const cursiv::Parser& p) {
  check(p, "boxed:", "a b c", box_of, "box: a b c");
  check(p, "boxed:", "a", box_of, "box: a");
  check(p, "boxed:", "", box_of, "box:");
}

void a_sequence_holding_one_node_gives_that_node(const cursiv::Parser& p) {
  check(p, "held:", "(a b)", box_of, "box: a b");
  check(p, "held:", "( a )", box_of, "box: a");
  check(p, "held:", "(a b", box_of, "<refused>");
}

void a_region_stays_with_its_sequence(const cursiv::Parser& p) {
  check(p, "tight:", "<<", text_of, "<<");
  check(p, "tight:", "< <", text_of, "<refused>");
  check(p, "skipped:", "<--<", text_of, "<<");
  check(p, "skipped:", "<<", text_of, "<<");
  check(p, "skipped:", "< <", text_of, "<refused>");
}

void a_choice_gives_the_value_of_its_arm(const cursiv::Parser& p) {
  check(p, "chosen:", "=", op_of, "equal");
  check(p, "chosen:", "!=", op_of, "NOTEQUAL");
  check(p, "chosen:", "x", op_of, "<refused>");
}

void every_engine(const cursiv::Parser& p) {
  a_sequence_stores_its_parts_joined(p);
  trivia_is_left_out(p);
  an_enumeration_takes_the_enumerator_of_that_name(p);
  a_text_member_takes_the_joined_spelling(p);
  append_adds_one_joined_element_per_turn(p);
  a_data_rule_call_is_one_part(p);
  conditions_around_one_element_keep_its_value(p);
  an_operand_that_builds_a_node_gives_that_node(p);
  a_sequence_holding_one_node_gives_that_node(p);
  a_region_stays_with_its_sequence(p);
  a_choice_gives_the_value_of_its_arm(p);
}

}  // namespace

void run() {
  engine = "Ll";
  every_engine(*ttree::generated::Ll::create_parser());
  engine = "Lr";
  every_engine(*ttree::generated::Lr::create_parser());

}

}  // namespace directive_sequence_cases

namespace data_parts_cases {

// A data rule's parts are its token occurrences and its data-rule calls: a
// call is one part, its rule's text, in both engines.

namespace {

using tdataparts::ast::Name;

struct Parsed {
  bool clean = false;
  std::string parts;
  std::string text;
};

// `misspelt`: the source holds one misspelling, which the parse replaces.
Parsed read(const cursiv::Parser& parser, std::string_view source,
            bool misspelt) {
  const std::string input = "dir:data:" + std::string(source);
  const cursiv::ParseResult r = parser.parse(input);
  const auto* name = r.value_as<Name>();
  const bool expected =
      misspelt ? r.diagnostics.size() == 1 &&
                     r.diagnostics[0].kind == cursiv::DiagnosticKind::replaced
               : r.diagnostics.empty();
  Parsed out;
  out.clean = r.accepted && expected && name != nullptr;
  if (out.clean) {
    out.parts = std::string(name->parts);
    out.text = std::string(name->text);
  }
  return out;
}

void check(const cursiv::Parser& parser, std::string_view source,
           std::string_view parts, std::string_view text = {},
           bool misspelt = false) {
  const Parsed got = read(parser, source, misspelt);
  if (!got.clean || got.parts != parts || got.text != text)
    std::printf("  %.*s\n    got      %s | %s\n    expected %.*s | %.*s\n",
                static_cast<int>(source.size()), source.data(),
                got.clean ? got.parts.c_str() : "<refused>", got.text.c_str(),
                static_cast<int>(parts.size()), parts.data(),
                static_cast<int>(text.size()), text.data());
  CHECK(got.clean && got.parts == parts && got.text == text);
}

void a_token_is_a_part(const cursiv::Parser& parser) {
  check(parser, "q a", "a");
  check(parser, "q a . b", "a|.|b");
  check(parser, "q a /* c */ . b", "a|.|b");
}

void a_data_rule_call_is_one_part(const cursiv::Parser& parser) {
  check(parser, "t a", "a");
  check(parser, "t a . b . c", "a|.b|.c");
  check(parser, "d a . b . c", "a|.b|.c");
  check(parser, "j a . b . c", "", "a.b.c");
}

void a_call_is_converted_when_its_converter_yields_text(
    const cursiv::Parser& parser) {
  check(parser, "u a ! b ! c", "a|!B|!C");
}

void a_unit_rule_converts_the_text_of_its_call(const cursiv::Parser& parser) {
  check(parser, "w a b c", "A|B|C");
}

void a_call_that_read_nothing_is_an_empty_part(const cursiv::Parser& parser) {
  check(parser, "o a b", "a||b");
}

void a_token_whose_converter_is_no_text_gives_its_spelling(
    const cursiv::Parser& parser) {
  check(parser, "n 7 , 300", "7|,|300");
}

void a_call_of_one_token_gives_that_token(const cursiv::Parser& parser) {
  check(parser, "p literal;", "literal");
  check(parser, "l a literal b", "a|LITERAL|b");
}

void a_misspelt_token_keeps_its_text_in_a_call(const cursiv::Parser& parser) {
  check(parser, "p litera;", "litera", {}, true);
  check(parser, "l a litera b", "a|LITERA|b", {}, true);
  check(parser, "k litera;", "", "LITERA", true);
}

void every_engine(const cursiv::Parser& parser) {
  a_token_is_a_part(parser);
  a_data_rule_call_is_one_part(parser);
  a_call_is_converted_when_its_converter_yields_text(parser);
  a_unit_rule_converts_the_text_of_its_call(parser);
  a_call_that_read_nothing_is_an_empty_part(parser);
  a_token_whose_converter_is_no_text_gives_its_spelling(parser);
  a_call_of_one_token_gives_that_token(parser);
  a_misspelt_token_keeps_its_text_in_a_call(parser);
}

}  // namespace

void run() {
  every_engine(*ttree::generated::Ll::create_parser());
  every_engine(*ttree::generated::Lr::create_parser());

}

}  // namespace data_parts_cases

namespace flag_operand_cases {

// A flag's operand reads and writes in the current node, in both engines: the
// flag opens no node of its own and sets its member on the node current once
// the operand has read -- the wrapper, if the operand nests.

namespace {

using namespace tflagop;

struct Parsers {
  std::unique_ptr<cursiv::Parser> parser;
  std::unique_ptr<cursiv::AstReflection> wrappedReflection;
};

const char* engine = "";

std::string yes(bool b) { return b ? "yes" : "no"; }

std::string show_expr(const ast::Expr* e) {
  if (e == nullptr) return "<null>";
  return std::string(e->text) + (e->parenthesized ? " parenthesized" : "");
}

std::string show_node_expr(const ast::Expr& e) { return show_expr(&e); }

std::string show_decl(const ast::Decl& d) {
  return std::string(d.name) + " hasInit=" + yes(d.hasInit) +
         " init=" + show_expr(d.init) + " always=" + yes(d.isConst) +
         " committed=" + yes(d.isStatic);
}

std::string show_list(const ast::Decl& d) {
  std::string out;
  for (const auto* dim : d.dims) {
    out += ' ';
    out += dim->name;
  }
  return " [" + out + " ]";
}

std::string show_counted(const ast::Decl& d) {
  return std::string(d.name) + " plus=" + yes(d.isStatic) +
         " minus=" + yes(d.isConst) + " list=" + yes(d.isArray) + show_list(d);
}

std::string show_turned(const ast::Decl& d) {
  return std::string(d.name) + " bang=" + yes(d.hasInit) +
         " turn=" + yes(d.raw) + show_list(d);
}

std::string show_dims(const ast::Decl& d) {
  return std::string(d.name) + " isArray=" + yes(d.isArray) + show_list(d);
}

std::string show_raw(const ast::Decl& d) {
  return std::string(d.name) + " raw=" + yes(d.raw) +
         " text=" + std::string(d.text);
}

std::string show_pair(const ast::Decl& d) {
  return std::string(d.name) + " static=" + yes(d.isStatic) +
         " const=" + yes(d.isConst) + " hasInit=" + yes(d.hasInit) +
         " init=" + show_expr(d.init) + " hasSize=" + yes(d.hasSize) +
         " size=" + std::string(d.size);
}

std::string show_doubled(const ast::Decl& d) {
  return std::string(d.name) + " isArray=" + yes(d.isArray) +
         " hasInit=" + yes(d.hasInit) + " init=" + show_expr(d.init);
}

std::string show_created(const ast::Decl& d) {
  return std::string(d.name) + " static=" + yes(d.isStatic);
}

template <class T>
std::string read(const cursiv::Parser& parser, std::string_view text,
                 std::string (*show)(const T&)) {
  const std::string source = "dir:" + std::string(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const root = r.value_as<T>();
  if (!r.accepted || !r.diagnostics.empty() || root == nullptr)
    return "<refused>";
  return show(*root);
}

void expect(const char* what, std::string_view text, const std::string& got,
            std::string_view expected) {
  if (got != expected)
    std::printf("  [%s] %s %.*s\n    got      %s\n    expected %.*s\n", engine,
                what, static_cast<int>(text.size()), text.data(), got.c_str(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
}

template <class T>
void check(const cursiv::Parser& parser, std::string_view text,
           std::string (*show)(const T&), std::string_view expected) {
  expect("", text, read<T>(parser, text, show), expected);
}

std::string wrapped_tree(const Parsers& p, std::string_view text) {
  const std::string source = "dir:wrapped:" + std::string(text);
  const cursiv::ParseResult r = p.parser->parse(source);
  if (!r.accepted || !r.diagnostics.empty() || r.value == nullptr)
    return "<refused>";
  const auto name = p.wrappedReflection->getTypeMetaData(r.value->type).name;
  if (name != "Wrap")
    return "decl " + std::string(r.value_as<ast::Decl>()->name);
  const auto* const wrap = r.value_as<ast::Wrap>();
  if (wrap->inner == nullptr) return "wrap <no inner>";
  return "wrap bang=" + yes(wrap->bang) + " of decl " +
         std::string(wrap->inner->name);
}

void a_flag_over_a_write_sets_its_member_and_writes_the_node(const Parsers& p) {
  check<ast::Decl>(*p.parser, "init:x = 1", show_decl,
                   "x hasInit=yes init=1 always=yes committed=yes");
  check<ast::Decl>(*p.parser, "init:x ;", show_decl,
                   "x hasInit=no init=<null> always=yes committed=yes");
  check<ast::Decl>(*p.parser, "init:x %1", show_decl,
                   "x hasInit=no init=<null> always=yes committed=yes");
  check<ast::Decl>(*p.parser, "init:x", show_decl,
                   "x hasInit=no init=<null> always=yes committed=yes");
  check<ast::Decl>(*p.parser, "init:x %", show_decl, "<refused>");
  check<ast::Decl>(*p.parser, "init:x =", show_decl, "<refused>");
}

void a_flag_over_a_repetition_that_reads_nothing_still_sets(const Parsers& p) {
  check<ast::Decl>(*p.parser, "counted:x", show_counted,
                   "x plus=yes minus=yes list=yes [ ]");
  check<ast::Decl>(*p.parser, "counted:x ++ --- a b", show_counted,
                   "x plus=yes minus=yes list=yes [ a b ]");
  check<ast::Decl>(*p.parser, "counted:x ----", show_counted, "<refused>");
  check<ast::Decl>(*p.parser, "turned:x", show_turned,
                   "x bang=no turn=no [ ]");
  check<ast::Decl>(*p.parser, "turned:x !", show_turned,
                   "x bang=yes turn=no [ ]");
  check<ast::Decl>(*p.parser, "turned:x a", show_turned,
                   "x bang=no turn=yes [ a ]");
  check<ast::Decl>(*p.parser, "turned:x **! ~a b", show_turned,
                   "x bang=yes turn=yes [ a b ]");
}

void a_flag_over_a_list_collects_into_the_node(const Parsers& p) {
  check<ast::Decl>(*p.parser, "dims:x(a, b)", show_dims, "x isArray=yes [ a b ]");
  check<ast::Decl>(*p.parser, "dims:x(a)", show_dims, "x isArray=yes [ a ]");
  check<ast::Decl>(*p.parser, "dims:x()", show_dims, "x isArray=yes [ ]");
  check<ast::Decl>(*p.parser, "dims:x", show_dims, "x isArray=no [ ]");
  check<ast::Decl>(*p.parser, "dims:x(a", show_dims, "<refused>");
}

void a_flag_over_a_value_write_takes_the_value(const Parsers& p) {
  check<ast::Decl>(*p.parser, "raw:x r 'abc'", show_raw, "x raw=yes text='abc'");
  check<ast::Decl>(*p.parser, "raw:x", show_raw, "x raw=no text=");
  check<ast::Decl>(*p.parser, "raw:x r", show_raw, "<refused>");
}

void a_flag_over_a_nest_sets_its_member_on_the_wrapper(const Parsers& p) {
  expect("wrapped", "x!", wrapped_tree(p, "x!"), "wrap bang=yes of decl x");
  expect("wrapped", "x", wrapped_tree(p, "x"), "decl x");
}

void flags_in_sequence_each_read_their_own_tokens(const Parsers& p) {
  check<ast::Decl>(*p.parser, "pair:x", show_pair,
                   "x static=no const=no hasInit=no init=<null> hasSize=no "
                   "size=");
  check<ast::Decl>(*p.parser, "pair:$ x = 1", show_pair,
                   "x static=yes const=no hasInit=yes init=1 hasSize=no "
                   "size=");
  check<ast::Decl>(*p.parser, "pair:$ # x [3]", show_pair,
                   "x static=yes const=yes hasInit=no init=<null> hasSize=yes "
                   "size=3");
  check<ast::Decl>(*p.parser, "pair:# x = 2 [4]", show_pair,
                   "x static=no const=yes hasInit=yes init=2 hasSize=yes "
                   "size=4");
  check<ast::Decl>(*p.parser, "pair:# $ x", show_pair, "<refused>");
}

void a_flag_over_a_call_on_an_empty_path_adopts_the_node(const Parsers& p) {
  check<ast::Expr>(*p.parser, "inside:(1)", show_node_expr, "1 parenthesized");
  check<ast::Expr>(*p.parser, "inside:1", show_node_expr, "<refused>");
  check<ast::Expr>(*p.parser, "outside:(1)", show_node_expr, "1 parenthesized");
  check<ast::Expr>(*p.parser, "outside:1", show_node_expr, "<refused>");
}

void an_adopting_flag_in_a_cycle_marks_the_node_it_returns(const Parsers& p) {
  check<ast::Expr>(*p.parser, "nested:1", show_node_expr, "1");
  check<ast::Expr>(*p.parser, "nested:(1)", show_node_expr, "1 parenthesized");
  check<ast::Expr>(*p.parser, "nested:((1))", show_node_expr, "1 parenthesized");
  check<ast::Expr>(*p.parser, "nested:((1)", show_node_expr, "<refused>");
}

void a_flag_over_a_bare_write_marks_the_node_it_writes(const Parsers& p) {
  check<ast::Decl>(*p.parser, "prefixed:++x", show_raw, "x raw=yes text=++");
  check<ast::Decl>(*p.parser, "prefixed:-- x", show_raw, "x raw=yes text=--");
  check<ast::Decl>(*p.parser, "prefixed:x", show_raw, "x raw=no text=");
  check<ast::Decl>(*p.parser, "prefixed:+x", show_raw, "<refused>");
}

void a_flag_inside_a_flag_sets_its_own_member(const Parsers& p) {
  check<ast::Decl>(*p.parser, "doubled:x(= 1)", show_doubled,
                   "x isArray=yes hasInit=yes init=1");
  check<ast::Decl>(*p.parser, "doubled:x", show_doubled,
                   "x isArray=no hasInit=no init=<null>");
  check<ast::Decl>(*p.parser, "doubled:x(=)", show_doubled, "<refused>");
}

void a_create_inside_a_flag_heads_the_node_the_flag_marks(const Parsers& p) {
  check<ast::Decl>(*p.parser, "created:$ x", show_created, "x static=yes");
  check<ast::Decl>(*p.parser, "created:x", show_created, "x static=no");
}

void every_engine(const Parsers& p) {
  a_flag_over_a_write_sets_its_member_and_writes_the_node(p);
  a_flag_over_a_repetition_that_reads_nothing_still_sets(p);
  a_flag_over_a_list_collects_into_the_node(p);
  a_flag_over_a_value_write_takes_the_value(p);
  a_flag_over_a_nest_sets_its_member_on_the_wrapper(p);
  flags_in_sequence_each_read_their_own_tokens(p);
  a_flag_over_a_call_on_an_empty_path_adopts_the_node(p);
  an_adopting_flag_in_a_cycle_marks_the_node_it_returns(p);
  a_flag_over_a_bare_write_marks_the_node_it_writes(p);
  a_flag_inside_a_flag_sets_its_own_member(p);
  a_create_inside_a_flag_heads_the_node_the_flag_marks(p);
}

Parsers ll_parsers() {
  return {ttree::generated::Ll::create_parser(),
          ttree::generated::Ll::create_ast_reflection()};
}

Parsers lr_parsers() {
  return {ttree::generated::Lr::create_parser(),
          ttree::generated::Lr::create_ast_reflection()};
}

}  // namespace

void run() {
  engine = "Ll";
  every_engine(ll_parsers());
  engine = "Lr";
  every_engine(lr_parsers());
}

}  // namespace flag_operand_cases
