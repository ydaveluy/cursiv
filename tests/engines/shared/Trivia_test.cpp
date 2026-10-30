#include "triviall/generated/Ll/Module.hpp"
#include "triviall/generated/Ll/Table.hpp"
#include "trivialr/generated/Lr/Module.hpp"
#include "trivialr/generated/Lr/Table.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/Checks.hpp"
#include "engines/shared/TriviaGrammar.hpp"
#include <memory>
#include <string>
#include <string_view>

namespace trivia_cases {

// Trivia in both engines: two ignored terminals reading one text are no tie,
// a blank keeps the refusal at its head, and a terminal read as trivia is no
// bracket of the indentation.

namespace {

void two_ignored_terminals_may_read_one_text(const cursiv::Parser& parser,
                                             std::string_view mode) {
  const std::string source = std::string(mode) + "a  b \t c";
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const root = r.value_as<ttrivia::ast::Node>();
  CHECK(r.accepted && r.diagnostics.empty() && root != nullptr);
  CHECK(root != nullptr && root->body.size() == 3);
}

// A blank that refuses two tabs at its head reads them inside it, and two
// that would open it are refused, however the lexer crosses blanks.
void a_blank_keeps_its_head_refusal(const cursiv::Parser& parser) {
  const cursiv::ParseResult inside = parser.parse("refusing:a \t\t b\tc");
  const auto* const root = inside.value_as<ttrivia::ast::Node>();
  CHECK(inside.accepted && inside.diagnostics.empty() && root != nullptr);
  CHECK(root != nullptr && root->body.size() == 3);
  for (const std::string_view text : {"refusing:a\t\tb", "refusing:\t\ta"}) {
    const cursiv::ParseResult opening = parser.parse(text);
    CHECK(!opening.accepted || !opening.diagnostics.empty());
  }
}

// The trivia in front of a region's first token is its caller's, which skips
// none here; the region's own blanks stand between its tokens and before its
// `eof`.
void a_region_head_is_its_callers(const cursiv::Parser& parser) {
  for (const std::string_view text :
       {"tied: a", "tied: ", "ranked: a", "bracketed: a", "refusing: a"}) {
    const cursiv::ParseResult r = parser.parse(text);
    CHECK(!r.accepted || !r.diagnostics.empty());
  }
  for (const std::string_view text : {"tied:a", "tied:a b ", "tied:"}) {
    const cursiv::ParseResult r = parser.parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
  }
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
  for (const auto make : {&ttrivia::generated::Ll::create_parser,
                          &ttrivia::generated::Lr::create_parser}) {
    two_ignored_terminals_may_read_one_text(*make(), "tied:");
    two_ignored_terminals_may_read_one_text(*make(), "ranked:");
    a_blank_keeps_its_head_refusal(*make());
    a_region_head_is_its_callers(*make());
  }
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
  same_text(engine, text, got, expected);
}

void a_closing_tag_ends_the_statement_before_it(const cursiv::Parser& parser) {
  check(parser, "<? a ?>", "a");
  check(parser, "<? a ? b : c ?>", "a(b,c)");
  check(parser, "<? a; b ?>", "a b");
  check(parser, "<? a b ?>", "<refused>");
}

}  // namespace

void run() {
  const struct {
    const char* prefix;
    const char* ll;
    const char* lr;
  } forms[] = {{"implied:", "SpelledLl", "SpelledLr"},
               {"implied:named:", "NamedLl", "NamedLr"},
               {"implied:apart:", "ApartLl", "ApartLr"}};
  for (const auto& form : forms) {
    prefix = form.prefix;
    engine = form.ll;
    a_closing_tag_ends_the_statement_before_it(
        *ttrivia::generated::Ll::create_parser());
    engine = form.lr;
    a_closing_tag_ends_the_statement_before_it(
        *ttrivia::generated::Lr::create_parser());
  }
}

}  // namespace implied_tag_cases

namespace region_cases {

// The gap in front of a region's first token and the gap behind its last are
// the caller's, which skips newlines; the region skips spaces and tabs only,
// so a newline between its tokens is refused.

namespace {

void spell(const tregion::ast::Item& item, std::string& out) {
  if (const auto* const pair = cursiv::ast_cast<tregion::ast::Pair>(&item)) {
    out += pair->key;
    if (!pair->value.empty()) {
      out += '=';
      out += pair->value;
    }
    return;
  }
  const auto& group = static_cast<const tregion::ast::Group&>(item);
  out += group.kind;
  out += '(';
  for (std::size_t i = 0; i < group.items.size(); ++i) {
    if (i != 0) out += ',';
    spell(*group.items[i], out);
  }
  out += ')';
}

const char* engine = "";

void check(const cursiv::Parser& parser, std::string_view text,
           std::string_view expected) {
  // The AST holds views into the text it was read from: keep it alive.
  const std::string source = "region:" + std::string(text);
  const cursiv::ParseResult r = parser.parse(source);
  const auto* const root = r.value_as<tregion::ast::Group>();
  std::string got = "<refused>";
  if (r.accepted && r.diagnostics.empty() && !r.cst().recovered() &&
      root != nullptr) {
    got.clear();
    spell(*root, got);
  }
  same_text(engine, text, got, expected);
}

void a_newline_stands_around_a_region(const cursiv::Parser& parser) {
  // First in an arm of a choice that a loop repeats, on its first turn and on
  // later ones, behind a region and behind a group.
  check(parser, "{\na b\n{\nc 1\n}\n2 d\ne f\n}\n", "({(a=b,{(c=1),2=d,e=f))");
  check(parser, "{ a b }", "({(a=b))");
  // The whole turn of a loop.
  check(parser, "[\nx = y\nz=w\n]", "([(x=y,z=w))");
  // First in a sequence, at the head of the document, with a newline behind.
  check(parser, "\na = b\n;\nc=d;", "(;(a=b),;(c=d))");
  check(parser, "\t\n x \t= y \n ;\n", "(;(x=y))");
  // An arm of a choice no loop repeats, last in the document.
  check(parser, "?\na 1\n", "(?(a=1))");
  check(parser, "?\n{\n1 2\n}", "(?({(1=2)))");
  // A region that opens on a loop, with turns and without.
  check(parser, "<\na b>\n", "(<(a,b))");
  check(parser, "<\n>", "(<())");
}

void a_newline_is_refused_inside_a_region(const cursiv::Parser& parser) {
  for (const std::string_view text :
       {"{ a\nb }", "{\n1\n2\n}", "[ x\n= y ]", "[ x =\ny ]", "a\n= b ;",
        "a = b\n;\nc\n= d;", "? a\n1", "<a\nb>", "<a\n>"})
    check(parser, text, "<refused>");
}

}  // namespace

void run() {
  engine = "Ll";
  a_newline_stands_around_a_region(*ttrivia::generated::Ll::create_parser());
  a_newline_is_refused_inside_a_region(
      *ttrivia::generated::Ll::create_parser());
  engine = "Lr";
  a_newline_stands_around_a_region(*ttrivia::generated::Lr::create_parser());
  a_newline_is_refused_inside_a_region(
      *ttrivia::generated::Lr::create_parser());
}

}  // namespace region_cases

namespace leading_cases {

// On the shape of a map file: a newline or a comment in front of a key, after
// a brush, is the entity's trivia; between a key and its value only blanks
// other than a newline stand.

namespace {

void read(const cursiv::Parser& parser, const char* engine) {
  const struct {
    const char* label;
    std::string_view source;
    bool clean;
    std::size_t brushes;
  } cases[] = {
      {"same line", "{ \"a\" \"b\" }\n", true, 0},
      {"newline before the key", "{\n\"a\" \"b\"\n}\n", true, 0},
      {"comment before the key", "{\n// caller comment\n\"a\" \"b\"\n}\n",
       true, 0},
      {"newline after a brush", "{\n{\n}\n\"a\" \"b\"\n}\n", true, 1},
      {"return between key and value", "{ \"a\"\r\"b\" }\n", true, 0},
      {"newline between key and value", "{ \"a\"\n\"b\" }\n", false, 0},
      {"comment between key and value",
       "{ \"a\" // not allowed here\n\"b\" }\n", false, 0},
  };
  for (const auto& sample : cases) {
    const std::string source = "leading:" + std::string(sample.source);
    const cursiv::ParseResult r = parser.parse(source);
    const bool clean =
        r.accepted && !r.cst().recovered() && r.diagnostics.empty();
    const auto* const doc = cursiv::ast_cast<tleading::ast::Document>(r.value);
    const auto* const entity =
        doc != nullptr && doc->entities.size() == 1 ? doc->entities[0] : nullptr;
    bool shape = entity != nullptr && entity->parent == doc &&
                 entity->keyvalues.size() == 1 &&
                 entity->brushes.size() == sample.brushes &&
                 r.cst().text() == source;
    if (shape) {
      const auto* const kv = entity->keyvalues[0];
      shape = kv->parent == entity && kv->key == "a" && kv->value == "b";
      for (const auto* const brush : entity->brushes)
        shape = shape && brush->parent == entity;
    }
    const bool ok = clean == sample.clean && (!sample.clean || shape);
    if (!ok)
      std::printf("  [%s] %s: clean %d, shape %d\n", engine, sample.label,
                  static_cast<int>(clean), static_cast<int>(shape));
    CHECK(ok);
  }
}

}  // namespace

void run() {
  read(*ttrivia::generated::Ll::create_parser(), "Ll");
  read(*ttrivia::generated::Lr::create_parser(), "Lr");
}

}  // namespace leading_cases

namespace lines_cases {

// A block's last entry reads no newline: the one in front of `end` is trivia
// to it, though a member on the next line reads it. The implied tags of this
// suite build its LR table with the lookaheads of each caller apart.

namespace {

void a_block_ends_before_its_newline(const cursiv::Parser& parser,
                                     const char* engine) {
  const struct {
    std::string_view text;
    std::size_t statements;
  } cases[] = {
      {"when\nb -> a\nelse -> a\nend", 1},
      {"when\nelse -> a = b\nend\nx", 2},
      {"when\nelse -> a\n.b\nend", 1},
  };
  for (const auto& sample : cases) {
    // The AST holds views into the text it was read from: keep it alive.
    const std::string source = "lines:" + std::string(sample.text);
    const cursiv::ParseResult r = parser.parse(source);
    const auto* const lines = r.value_as<tunary::ast::Lines>();
    const std::size_t read = lines != nullptr ? lines->items.size() : 0;
    const bool ok = r.accepted && r.diagnostics.empty() &&
                    !r.cst().recovered() && read == sample.statements;
    if (!ok)
      std::printf("  [%s] %.*s: %zu diagnostics, %zu statements\n", engine,
                  static_cast<int>(sample.text.size()), sample.text.data(),
                  r.diagnostics.size(), read);
    CHECK(ok);
  }
}

}  // namespace

void run() {
  a_block_ends_before_its_newline(*ttrivia::generated::Ll::create_parser(),
                                  "Ll");
  a_block_ends_before_its_newline(*ttrivia::generated::Lr::create_parser(),
                                  "Lr");
}

}  // namespace lines_cases

int main() {
  cursiv::test::count_expectations();
  trivia_cases::run();
  implied_tag_cases::run();
  region_cases::run();
  leading_cases::run();
  lines_cases::run();
  return cursiv::test::report("Trivia_test");
}
