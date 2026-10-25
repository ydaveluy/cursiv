#include "triviall/generated/Ll/Module.hpp"
#include "triviall/generated/Ll/Table.hpp"
#include "trivialr/generated/Lr/Module.hpp"
#include "trivialr/generated/Lr/Table.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/TriviaGrammar.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace trivia_cases {

// Trivia in both engines: two ignored terminals reading one text are no tie,
// and a terminal read as trivia is no bracket of the indentation.

namespace {

void two_ignored_terminals_may_read_one_text(const cursiv::Parser& parser,
                                             std::string_view mode) {
  const std::string source = std::string(mode) + "a  b \t c";
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const root = r.value_as<ttrivia::ast::Node>();
  CHECK(r.accepted && r.diagnostics.empty() && root != nullptr);
  CHECK(root != nullptr && root->body.size() == 3);
}

template <const auto& Table>
constexpr const auto& cfg_of() {
  if constexpr (requires { Table.cfg.terms; })
    return Table.cfg;
  else
    return Table;
}

template <const auto& Table>
void a_trivia_terminal_is_no_bracket() {
  constexpr const auto& cfg = cfg_of<Table>();
  unsigned note = 0, open = 0, close = 0;
  for (std::uint16_t t = 1; t < cfg.nterms; ++t) {
    const auto name = cfg.term_name(t);
    if (name == "NOTE" && cfg.terms[t].recorded) {
      CHECK(cfg.terms[t].trivia);
      CHECK(cfg.terms[t].delim == 0);
      ++note;
    } else if (name == "(") {
      CHECK(cfg.terms[t].delim == 1);
      ++open;
    } else if (name == ")") {
      CHECK(cfg.terms[t].delim == -1);
      ++close;
    }
  }
  CHECK(note == 2 && open == 1 && close == 1);
}

}  // namespace

void run() {
  two_ignored_terminals_may_read_one_text(
      *ttrivia::generated::Ll::create_parser(), "tied:");
  two_ignored_terminals_may_read_one_text(
      *ttrivia::generated::Lr::create_parser(), "tied:");
  two_ignored_terminals_may_read_one_text(
      *ttrivia::generated::Ll::create_parser(), "ranked:");
  two_ignored_terminals_may_read_one_text(
      *ttrivia::generated::Lr::create_parser(), "ranked:");
  a_trivia_terminal_is_no_bracket<ttrivia::generated::Ll::kTable>();
  a_trivia_terminal_is_no_bracket<ttrivia::generated::Lr::kTable>();

}

}  // namespace trivia_cases

namespace implied_tag_cases {

// The text an implied token's condition reads lexes whole wherever the token
// may be implied: `?>` closes the statement, and is never taken as a `?`.

namespace {

void spell(const timpliedtag::ast::Stmt& stmt, std::string& out) {
  out += stmt.head;
  if (stmt.args.empty()) return;
  out += '(';
  for (std::size_t i = 0; i < stmt.args.size(); ++i) {
    if (i != 0) out += ',';
    spell(*stmt.args[i], out);
  }
  out += ')';
}

std::string_view prefix;

std::string read(const cursiv::Parser& parser, std::string_view text) {
  const std::string source = std::string(prefix) + std::string(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const doc = r.value_as<timpliedtag::ast::Doc>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr)
    return "<refused>";
  std::string out;
  for (const auto* const stmt : doc->body) {
    if (!out.empty()) out += ' ';
    spell(*stmt, out);
  }
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

void a_closing_tag_ends_the_statement_before_it(const cursiv::Parser& parser) {
  check(parser, "<? a ?>", "a");
  check(parser, "<? a ? b : c ?>", "a(b,c)");
  check(parser, "<? a; b ?>", "a b");
  check(parser, "<? a b ?>", "<refused>");
}

}  // namespace

void run() {
  prefix = "implied:";
  engine = "SpelledLl";
  a_closing_tag_ends_the_statement_before_it(
      *ttrivia::generated::Ll::create_parser());
  engine = "SpelledLr";
  a_closing_tag_ends_the_statement_before_it(
      *ttrivia::generated::Lr::create_parser());
  prefix = "implied:named:";
  engine = "NamedLl";
  a_closing_tag_ends_the_statement_before_it(
      *ttrivia::generated::Ll::create_parser());
  engine = "NamedLr";
  a_closing_tag_ends_the_statement_before_it(
      *ttrivia::generated::Lr::create_parser());
  prefix = "implied:apart:";
  engine = "ApartLl";
  a_closing_tag_ends_the_statement_before_it(
      *ttrivia::generated::Ll::create_parser());
  engine = "ApartLr";
  a_closing_tag_ends_the_statement_before_it(
      *ttrivia::generated::Lr::create_parser());

}

}  // namespace implied_tag_cases

int main() {
  cursiv::test::count_expectations();
  trivia_cases::run();
  implied_tag_cases::run();
  return cursiv::test::report("Trivia_test");
}
