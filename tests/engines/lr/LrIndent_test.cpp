// A suite opens where a line is deeper than the one that opened it and
// closes where a line comes back, one dedent per level, whatever the run is
// made of; the input's end closes what is open.

#include <cstdio>
#include <string>
#include <string_view>

#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/IndentAst.hpp"
#include "engines/lr/ParseChecks.hpp"
#include "engines/lr/VirtualStateGrammar.hpp"
#include "indent/generated/G/Module.hpp"
#include "indentfolded/generated/Cases/Module.hpp"
#include "indentfolded/generated/Cases/Parser.hpp"
#include "indentfolded/generated/Cases/Table.hpp"
#include "indentimplied/generated/Cases/Module.hpp"
#include "indentimplied/generated/Cases/Parser.hpp"
#include "indentimplied/generated/Cases/Table.hpp"
#include "indentrepaired/generated/G/Module.hpp"

namespace {

// The tree as `name(child child)` words, or empty where the parse complained.
void spell(const tindent::ast::Node* n, std::string& out) {
  for (const auto* const k : n->body) {
    if (out.empty() || out.back() != '(') out += ' ';
    out += k->name;
    if (!k->body.empty()) {
      out += '(';
      spell(k, out);
      out += ')';
    }
  }
}

template <class Engine>
std::string parse_with(const Engine& engine, std::string_view text) {
  const cursiv::ParseResult r = engine->parse(text);
  const auto* const doc = r.value_as<tindent::ast::Node>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr) return "";
  std::string out;
  spell(doc, out);
  if (!out.empty()) out.erase(0, 1);
  return out;
}

std::string parse(std::string_view text) {
  return parse_with(tindent::generated::G::create_parser(), text);
}

bool recovers(std::string_view text) {
  return checks::repaired(*tindent::generated::G::create_parser(), text);
}

// Where the first statement's tree ends, or -1 where the parse complained.
template <class Engine>
long first_end(const Engine& engine, std::string_view text) {
  const cursiv::ParseResult r = engine->parse(text);
  const auto* const doc = r.value_as<tindent::ast::Node>();
  if (!r.accepted || !r.diagnostics.empty() || doc == nullptr ||
      doc->body.empty())
    return -1;
  return r.cst()[doc->body[0]->cstNodeId].end;
}

// Whether the caret over `|..|` offers the spelling `literal`.
bool offers_over(const cursiv::Parser& engine, std::string text,
                 std::string_view literal) {
  const checks::Marked from(std::move(text)), to(from.text);
  return checks::offers(engine, to.text, from.at, to.at, literal);
}

// Whether the caret at `at` is offered the terminal named `name`.
bool offers(const cursiv::Parser& engine, std::string_view text,
            cursiv::TextOffset at, std::string_view name) {
  return checks::offers_named(engine, text, at, at, name);
}

void an_indent_keeps_an_earlier_repair_reading_alive() {
  namespace g = tindent::folded::generated::Cases;
  const auto engine = g::create_parser();
  for (const unsigned count : {40u, 80u}) {
    std::string prefix = "validation x";
    for (unsigned i = 0; i != count; ++i) prefix += " a";
    for (const std::string_view newline : {"\n", "\r\n"}) {
      for (const std::string_view indent : {"  ", "\t"}) {
        const std::string tail = std::string(newline) + std::string(indent) + "z";
        const auto r = engine->parse(prefix + tail);
        CHECK(r.accepted && r.diagnostics.size() == 1);
        const auto* node = r.value_as<tvirtual::Node>();
        CHECK(node && node->first && !node->second);
        for (const auto& d : r.diagnostics) {
          CHECK(d.kind == cursiv::DiagnosticKind::inserted);
          CHECK(d.begin == prefix.size() && d.end == prefix.size());
          const auto spellings =
              engine->getGrammar().terminalSpellings(d.element);
          CHECK(spellings.size() == 1 && spellings.front() == ";");
        }
        unsigned invented = 0;
        r.cst().visit_leaves([&](const cursiv::CstLeaf& leaf) {
          if (leaf.recovered && leaf.begin == leaf.end) ++invented;
        });
        CHECK(invented == 1);

        cursiv::Cst cst;
        cursiv::LrParseStats stats;
        CHECK(cursiv::lr_parser<g::kTable>.parse(prefix + tail, cst, &stats,
                                                 &g::kDrive));
        CHECK(stats.splits > 0 && stats.episodeSearches > 0);
        for (const auto& text : {prefix + ";" + tail, prefix + tail + "!@$"}) {
          const auto valid = engine->parse(text);
          CHECK(valid.accepted && valid.diagnostics.empty());
          const auto* root = valid.value_as<tvirtual::Node>();
          CHECK(root && root->first == (text == prefix + ";" + tail) &&
                root->second == (text == prefix + tail + "!@$"));
        }
      }
    }
  }
}

// The look-ahead reads on past the indent or the dedent that tells two
// readings of a line apart: the cell settles there, no graph.
void an_indent_settles_a_cell_without_the_graph() {
  namespace g = tindent::laid::generated::Cases;
  const std::string text = "fork a b\n  c\n    d\n  e\nf\n";
  CHECK(parse_with(g::create_parser(), text) == "ab(c(d) e) f");
  cursiv::Cst cst;
  cursiv::LrParseStats stats;
  CHECK(cursiv::lr_parser<g::kTable>.parse(text, cst, &stats, &g::kDrive));
  CHECK(stats.splits == 0);
}

}  // namespace

int main() {
  an_indent_keeps_an_earlier_repair_reading_alive();
  an_indent_settles_a_cell_without_the_graph();
  CHECK(parse("a\nb\n") == "a b");
  CHECK(parse("a:\n  b\n  c\nd\n") == "a(b c) d");
  CHECK(parse("a:\n  b:\n    c\n  d\ne\n") == "a(b(c) d) e");
  // Two levels close at once, and the end of input closes what is open.
  CHECK(parse("a:\n  b:\n    c\nd\n") == "a(b(c)) d");
  CHECK(parse("a:\n  b:\n    c\n") == "a(b(c))");
  CHECK(parse("a:\n  b:\n    c") == "a(b(c))");
  // The run is compared as text: tabs, two spaces, eight -- any run deeper
  // than the enclosing one opens, and blank lines in between count for nothing.
  CHECK(parse("a:\n\tb:\n\t\tc\n\n\td\n\ne\n") == "a(b(c) d) e");
  CHECK(parse("a:\n        b\nc:\n  d\n") == "a(b) c(d)");
  // A form feed starts the run again, as in Python: a line of form feeds is
  // blank, the blanks after the last one are the line's run, and one past
  // content is plain trivia.
  CHECK(parse("a:\n  b\n\f\nc\n") == "a(b) c");
  CHECK(parse("a:\n  b:\n    c\n\fd\n") == "a(b(c)) d");
  CHECK(recovers("a:\n  b\n \f c\n"));
  // ...and on the last line, where no line ending follows.
  CHECK(parse("a\n\f") == "a");
  CHECK(parse("a:\n  b\n\f") == "a(b)");
  CHECK(parse("a:\n  b\n  \f") == "a(b)");
  CHECK(parse("a:\n  b:\n    c\n\f") == "a(b(c))");
  CHECK(parse("a:\n  b\nc\n\f") == "a(b) c");
  CHECK(parse("a:\n  b\f") == "a(b)");
  CHECK(parse("a:\n  b \f\n") == "a(b)");
  // A line back to no level that was opened is no dedent the grammar takes.
  CHECK(recovers("a:\n    b\n  c\n"));
  // A deeper line where no suite opens is an error, and the parse goes on.
  CHECK(recovers("a\n  b\nc\n"));
  CHECK(parse("a:\n  b\n") == "a(b)");
  // EOF also ends a blank line: its run must neither open a suite nor
  // disagree with an existing level, even when tabs and spaces differ.
  for (const std::string_view blank : {"  ", "\t", " \t "}) {
    CHECK(parse("a\n" + std::string(blank)) == "a");
    CHECK(parse("a:\n  b\n" + std::string(blank)) == "a(b)");
    const auto r = tindent::generated::G::create_parser()->parse(blank);
    CHECK(r.accepted && r.diagnostics.empty());
  }
  CHECK(parse("a:\n  b:\n    c\n      ") == "a(b(c))");
  // A dedent the end of input owes stands there, behind the trivia of the
  // suite's last line: a line at the suite's level is the suite's.
  {
    const auto plain = tindent::generated::G::create_parser();
    CHECK(first_end(plain, "a:\n  b #x") == 9);
    CHECK(first_end(plain, "a:\n  b\n  #x") == 11);
    CHECK(first_end(plain, "a:\n  b:\n    c\n  #x") == 18);
    // ...whether or not the end of input stands for a statement's newline.
    const auto laid = tindent::laid::generated::Cases::create_parser();
    CHECK(parse_with(laid, "a:\n  b\n  c") == "a(b c)");
    CHECK(first_end(laid, "a:\n  b #x") == 9);
    CHECK(first_end(laid, "a:\n  b\n  #x") == 11);
    CHECK(first_end(laid, "a:\n  b:\n    c\n  #x") == 18);
  }
  CHECK(parse("a\r\n  ") == "a");
  // Inside brackets a line is a line: no level opens or closes, and a
  // continued line starts no logical line.
  CHECK(parse("a[\n    b\n  c\n]\nd\n") == "a(b c) d");
  CHECK(parse("a:\n  b \\\n\n  d\n") == "a(b d)");
  CHECK(parse("a:\n  b #x\\\nc\n") == "a(b) c");
  // A run neither at a level nor deeper by one -- a tab against spaces --
  // is an error, not a line at the same level.
  CHECK(recovers("a:\n\tb\n  c\nd\n"));
  CHECK(parse("a:\n\tb\n  c\nd\n").empty());
  // A level is as wide as its run, whatever the width.
  const std::string wide(70000, ' ');
  CHECK(parse("a:\n" + wide + "b\n" + wide + "c\nd\n") == "a(b c) d");
  CHECK(recovers("a:\n" + wide + "b\n  c\nd\n"));
  // The caret replays the indentation's tokens as the tape holds them: a
  // suite's lines are offered past an indent and past a dedent alike.
  const auto engine = tindent::generated::G::create_parser();
  CHECK(offers(*engine, "a:\n  b\n  c\n", 5, "ID"));
  CHECK(offers(*engine, "a:\n  b\n  c\n", 9, "ID"));
  CHECK(offers(*engine, "a:\n  b\n  c\n", 11, "ID"));
  CHECK(offers(*engine, "a:\n  b\nc\n", 7, "ID"));
  // Where the text ends the caret's line reads its own run: two levels
  // close before a name, and the first line of a suite opens one.
  CHECK(offers(*engine, "a:\n  b:\n    c\n", 14, "ID"));
  CHECK(offers(*engine, "a:\n  ", 5, "ID"));
  CHECK(!offers(*engine, "a:\n  ", 5, "INDENT"));
  CHECK(!offers(*engine, "a:\n  b\n", 7, "DEDENT"));
  // Inside a bracket a newline is a blank, even where the state before it
  // reads the statement's newline: the list goes on past it.
  {
    const auto joined = tindent::generated::G::create_parser();
    for (const auto text : {"a[b,\n  c]\n", "a[b\n  ,c]\n", "a:\n  b[c\n  ]\n"}) {
      const std::string source = "joined " + std::string(text);
      const auto r = joined->parse(source);
      CHECK(r.accepted && r.diagnostics.empty());
    }
    CHECK(offers(*joined, "joined a[b,\n  ", 14, "ID"));
    CHECK(offers_over(*joined, "joined a[b\n  ||]\n", "]"));
    CHECK(offers_over(*joined, "joined a[b\n  ||]\n", ","));
    CHECK(offers_over(*joined, "joined a[b[c,\n  d\n  ||", ","));
  }
  // A comment that ends at the caret goes on over what is typed there: no
  // word is offered inside it, and the line's own end still is.
  CHECK(!offers_over(*engine, "a # c||\nb\n", "["));
  CHECK(!offers_over(*engine, "a # c||\nb\n", ":"));
  CHECK(offers(*engine, "a # c\nb\n", 5, "NEWLINE"));
  CHECK(offers_over(*engine, "a ||# c\nb\n", "["));
  // What the end of input lays behind the comment, the closers of the open
  // suites, does not hide it.
  CHECK(!offers_over(*engine, "a:\n  b #x||", ":"));
  CHECK(!offers_over(*engine, "a:\n  b #x||", "["));
  const auto bare = tindent::repaired::generated::G::create_parser();
  CHECK(parse_with(bare, "a:\n  b:\n    c\n  d\ne\n") == "a(b(c) d) e");
  CHECK(parse_with(bare, "a:\n  b\n").empty() == false);
  // The indentation is read where the newline ends. One that carries the
  // lines of comments leaves a line of content there, whatever column a
  // comment stands at; a comment the newline leaves is a line of content.
  const auto folded = tindent::folded::generated::Cases::create_parser();
  CHECK(parse_with(folded, "a:\n  b\n#x\n  c\n") == "a(b c)");
  CHECK(parse_with(folded, "a:\n  b\n    #x\n  c\n") == "a(b c)");
  CHECK(parse_with(folded, "a:\n  b\n  #x\nc\n") == "a(b) c");
  CHECK(parse_with(folded, "a:\n#x\n  b\n") == "a(b)");
  CHECK(parse_with(folded, "a:\n  b\n#x\n#y\n  c\nd\n") == "a(b c) d");
  CHECK(parse_with(folded, "a:\n  b #x\n  c\n") == "a(b c)");
  {
    const auto r = folded->parse("a:\n  b\n  #x\n      c\n");
    CHECK(r.accepted && !r.diagnostics.empty());
  }
  const auto repaired = tindent::repaired::generated::G::create_parser();
  for (const auto text : {"a[[[b]]?\nc\n", "a[[[\n  b\n]]?\nc\n",
                          "a[[?\n  b\n]]]\nc\n"}) {
    const auto r = repaired->parse(text);
    CHECK(r.accepted && r.diagnostics.size() == 1);
    if (r.diagnostics.size() == 1)
      CHECK(r.diagnostics[0].kind == cursiv::DiagnosticKind::replaced);
    const auto* doc = r.value_as<tindent::ast::Node>();
    CHECK(doc != nullptr);
    std::string tree;
    if (doc) spell(doc, tree);
    CHECK(tree == " a(b) c");
  }
  std::printf("  the suite opens on a deeper line and closes on a shallower\n");
  // The first line of a suite is offered its keywords as the second is.
  {
    const auto kw = tindent::laid::generated::Cases::create_parser();
    CHECK(offers_over(*kw, "suites top a:\n  |pa|", "pass"));
    CHECK(offers_over(*kw, "suites top a:\n  |pass|\n", "pass"));
    CHECK(offers_over(*kw, "suites top a:\n  pass\n  |pa|", "pass"));
    // A line back at no level closes the suite; one deeper opens a level
    // no statement takes.
    CHECK(offers_over(*kw, "suites top a:\n  pass\n||", "top"));
    CHECK(!offers_over(*kw, "suites top a:\n  pass\n||", "pass"));
    CHECK(!offers_over(*kw, "suites top a:\n  pass\n    ||", "pass"));
    // A dedent the tape laid before dropped text closed the suite.
    CHECK(offers_over(*kw, "suites top a:\n  pass\n@@ ||", "top"));
    CHECK(!offers_over(*kw, "suites top a:\n  pass\n@@ ||", "pass"));
  }
  // A bracket its condition refuses opens nothing: the suite after it reads
  // its levels.
  {
    const auto guarded = tindent::generated::G::create_parser();
    CHECK(parse_with(guarded, "guarded a[b]\nc:\n  d\ne\n") == "a(b) c(d) e");
    for (const std::string_view text :
         {"a [b]\nc:\n  d\ne\n", "a [b\nc:\n  d\ne\n"}) {
      const std::string source = "guarded " + std::string(text);
      const auto r = guarded->parse(source);
      CHECK(r.accepted && !r.diagnostics.empty());
      for (const auto& d : r.diagnostics) CHECK(d.begin < 8 + text.find('\n'));
    }
    // A record the tape closes before the token a repair invents for it is
    // walked, however far past it the caret stands.
    std::string far = "  ] \n    #x:  ]]]block[    aother\t\n  ]b    ";
    for (int i = 0; i < 60; ++i) far += "\nz";
    CHECK(offers(*guarded, "guarded " + far,
                 static_cast<cursiv::TextOffset>(far.size() + 8),
                 "NEWLINE"));
  }
  // A rule may open a bracket or a suite that its caller closes: past the
  // rule, the caret still stands inside them.
  {
    const auto split = tindent::generated::G::create_parser();
    for (const std::string_view text :
         {"[a,\n pass]\n", "block b:\n  pass\n  pass\n"}) {
      const std::string source = "joined " + std::string(text);
      const auto r = split->parse(source);
      CHECK(r.accepted && r.diagnostics.empty());
    }
    CHECK(offers_over(*split, "joined [a, |pass|]\n", "pass"));
    CHECK(offers_over(*split, "joined [a,\n |pass|]\n", "pass"));
    CHECK(offers_over(*split, "joined block b:\n  pass\n  |pass|\n", "pass"));
  }
  return cursiv::test::report("LrIndent_test");
}
