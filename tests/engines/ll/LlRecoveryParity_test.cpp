// A broken document is repaired as the table engine repairs it: the
// diagnostics and the tree agree, down to what they name. Each text is the
// smallest of a kind where the descent used to destroy more of the document
// than the table engine does, or to name another repair for it.

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "conditionsll/generated/Ll/Module.hpp"
#include "conditionslr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/validation/RepairMessage.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/ParityAst.hpp"
#include "parityll/generated/Ll/Module.hpp"
#include "paritylr/generated/Lr/Module.hpp"

namespace {

// The descent and the table engine of one language, with their reflections.
struct Engines {
  std::unique_ptr<cursiv::Parser> ll, lr;
  std::unique_ptr<cursiv::AstReflection> llr, lrr;
};

Engines parity() {
  namespace g = tpar::generated;
  return {g::Ll::create_parser(), g::Lr::create_parser(),
          g::Ll::create_ast_reflection(), g::Lr::create_ast_reflection()};
}

void agree(const Engines& e, std::string_view text, bool repaired = true) {
  const auto a = e.ll->parse(text);
  const auto b = e.lr->parse(text);
  CHECK(a.accepted && b.accepted);
  CHECK(a.cst().recovered() == repaired && b.cst().recovered() == repaired);
  CHECK(a.diagnostics.empty() == !repaired &&
        b.diagnostics.empty() == !repaired);
  CHECK(a.value != nullptr && b.value != nullptr);
  bool alike = a.diagnostics.size() == b.diagnostics.size();
  for (std::size_t i = 0; alike && i < a.diagnostics.size(); ++i) {
    const auto& x = a.diagnostics[i];
    const auto& y = b.diagnostics[i];
    alike = x.kind == y.kind && x.begin == y.begin && x.end == y.end &&
            x.element == y.element && x.also == y.also;
  }
  if (!alike) {
    std::fprintf(stderr, "repair parity [%.*s]\n",
                 static_cast<int>(text.size()), text.data());
    const auto notes = [](const char* engine, const auto& result) {
      for (const auto& d : result.diagnostics)
        std::fprintf(stderr, "  %s: kind=%u span=[%u,%u) element=%u also=%zu\n",
                     engine, static_cast<unsigned>(d.kind), d.begin, d.end,
                     d.element, d.also.size());
    };
    notes("LL", a);
    notes("LR", b);
  }
  CHECK(alike);
  if (a.value == nullptr || b.value == nullptr) return;
  std::string x, y;
  cursiv::test::ast_of(*e.llr, *a.value, x);
  cursiv::test::ast_of(*e.lrr, *b.value, y);
  if (x != y)
    std::fprintf(stderr, "tree parity [%.*s]\n  LL: %s\n  LR: %s\n",
                 static_cast<int>(text.size()), text.data(), x.c_str(),
                 y.c_str());
  CHECK(x == y);
}

// Missing operands and delimiters, a word read again, or text dropped between
// statements all leave the following statement in the same tree. These also
// pin what the repair calls its missing terminal, and any tied alternatives.
void a_repair_keeps_the_following_statement() {
  const auto e = parity();
  for (const std::string_view text :
       {"show x as 2;", "show x as ; show y;", "let a = 1 + ; show b;",
        "let a = (1 + 2; show b;", "let a = f(1 2); show b;",
        "let a = f(1,); show b;", "let a = 1 ? + 2; show b;",
        "export lt a = 1; show b;", "show x y; let b = 2;",
        "let a = 1; @ show b;", "let a = 1; /* gap */ @ show b;",
        "let a = f(1, 2", "show x as", "export", "let a = (1, 2);",
        "show (1, 2);", "let a = f(1,)!; show a;",
        "let a = f(1, @2, 3)!; show a;", "let a = (; s~ow a;", "(;s~ow"})
    agree(e, text);
  for (const std::string_view text :
       {"", "let a = 1; show b;", "show let, export;",
        "export let a = f(1, 2); /* gap */ show a as b;"})
    agree(e, text, false);
}

// A prediction can discover a fault far beyond a choice's common prefix.
// Reading a few clean tokens there must not settle the choice before recovery
// reaches the missing semicolon, inventing a postfix operator along the way.
void a_late_fault_does_not_commit_an_optional_postfix() {
  const auto e = parity();
  std::string text = "let a = f(";
  for (unsigned i = 0; i < 80; ++i) {
    if (i != 0) text += ',';
    text += '1';
  }
  text += ')';
  agree(e, text);
}

// A plan owing more closers than a repair may invent unplanned writes them
// all: the statement after the run stays in the tree.
void a_planned_run_of_closers_is_written_whole() {
  const auto e = parity();
  const std::string text = "let a = " + std::string(20, '(') + "1;\nlet b = 2;";
  agree(e, text);
  for (const cursiv::Parser* parser : {e.ll.get(), e.lr.get()}) {
    const auto parsed = parser->parse(text);
    const auto* unit = parsed.value_as<tpar::ast::Unit>();
    CHECK(unit && unit->lets.size() == 2);
    if (unit != nullptr && unit->lets.size() == 2)
      CHECK(unit->lets[1] && unit->lets[1]->name == "b");
  }
}

// Whether a leaf the repair marked lies over the bytes in [begin, end).
bool marks(const cursiv::ParseResult& r, cursiv::TextOffset begin,
           cursiv::TextOffset end) {
  const cursiv::Cst& cst = r.cst();
  for (cursiv::NodeId i = 0; i < cst.size(); ++i) {
    const cursiv::CstNode& n = cst[i];
    if (n.subtreeSize == 1 && n.recovered && n.begin < end && begin < n.end)
      return true;
  }
  return false;
}

// Bytes no terminal reads stop where a token the lexer left open starts: that
// token is repaired as it would be with nothing in front of it. A trivia the
// lexer refused is laid as the author's text, unmarked.
void unmatched_bytes_stop_at_an_open_token() {
  namespace c = tllconditions::generated;
  const Engines e{c::Ll::create_parser(), c::Lr::create_parser(),
                  c::Ll::create_ast_reflection(),
                  c::Lr::create_ast_reflection()};
  for (const std::string_view text :
       {"one; @/* x", "one; /* x", "{ one; /* x", "one; @@/* x"})
    agree(e, text);
  for (const cursiv::Parser* parser : {e.ll.get(), e.lr.get()}) {
    const auto parsed = parser->parse("one; @/* x");
    CHECK(marks(parsed, 5, 6) && !marks(parsed, 6, 10));
    CHECK(!marks(parser->parse("{ one; /* x"), 7, 11));
  }
}

// A token no construct takes at the head of a statement, or in front of what
// closes one: the repair is one insertion or one replacement, and the text
// behind it is read.
void a_stray_token_costs_one_repair() {
  const auto e = parity();
  for (const std::string_view text :
       {",2;", "as;", "+2;", "y,", ")2;", "w,", "a,", "a 2", "@2;", "/2;",
        "xport;", "w 2", "le;", "et;", ";2;", "/y;", "\"2;"})
    agree(e, text);
}

// Declarations that open alike and part on their second word: a misspelling of
// it is read again, not made up for by a construct invented in front of it.
void a_misspelt_second_word_is_read_again() {
  const auto e = parity();
  for (const auto& [text, count] :
       {std::pair{"decl:pub strcut a {}", 1u},
        std::pair{"decl:pub strut a {}", 1u},
        std::pair{"decl:pub struct a {} pub strcut b {}", 2u}}) {
    agree(e, text);
    const auto parsed = e.ll->parse(text);
    const auto* unit = parsed.value_as<tpar::ast::Unit>();
    CHECK(unit && unit->lets.size() == count && unit->shows.empty());
    if (unit != nullptr)
      for (std::size_t i = 0; i < unit->lets.size(); ++i)
        CHECK(unit->lets[i] && unit->lets[i]->exported &&
              unit->lets[i]->name == (i == 0 ? "a" : "b"));
  }
  agree(e, "decl:pub struct a {}", false);
  agree(e, "decl:pub class a extends b {}", false);
  for (const cursiv::Parser* parser : {e.ll.get(), e.lr.get()})
    CHECK(!parser->parse("decl:/* gap */pub struct a {}").diagnostics.empty());
}

// Between two operands, every operator is as cheap a repair as the one
// elected, whatever level it binds at: both engines name the same ones, though
// the readings each search merges are its own. An operator a tighter level
// refuses is the next looser level's, and that level writes the planned one.
void a_missing_operator_names_every_level() {
  const auto e = parity();
  for (const std::string_view text :
       {"show 1 = 2 * 3;", "show f(1) = 2 + 3;", "let a = 1 = 2 * 3;",
        "show 1 2 * 3;", "show (1 2 3);", "let a = (1 2 3); show a;"})
    agree(e, text);
  const std::string_view text = "show 1 = 2 * 3;";
  for (const cursiv::Parser* parser : {e.ll.get(), e.lr.get()}) {
    const auto parsed = parser->parse(text);
    CHECK(parsed.diagnostics.size() == 1);
    if (parsed.diagnostics.size() != 1) continue;
    const cursiv::ParseDiagnostic& d = parsed.diagnostics.front();
    CHECK(cursiv::repair_message(parser->getGrammar(), d, text) ==
          "Expecting one of '*', ',', '/', '+' but found '='.");
  }
  // The second operator is planned with the first: it names nothing more.
  for (const cursiv::Parser* parser : {e.ll.get(), e.lr.get()}) {
    const auto parsed = parser->parse("show (1 2 3);");
    CHECK(parsed.diagnostics.size() == 2);
    if (parsed.diagnostics.size() == 2)
      CHECK(!parsed.diagnostics[0].also.empty() &&
            parsed.diagnostics[1].also.empty());
  }
}

}  // namespace

int main() {
  a_missing_operator_names_every_level();
  a_stray_token_costs_one_repair();
  a_misspelt_second_word_is_read_again();
  a_repair_keeps_the_following_statement();
  a_late_fault_does_not_commit_an_optional_postfix();
  a_planned_run_of_closers_is_written_whole();
  unmatched_bytes_stop_at_an_open_token();
  return cursiv::test::report("LlRecoveryParity_test");
}
