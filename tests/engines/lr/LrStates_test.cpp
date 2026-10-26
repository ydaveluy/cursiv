#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "continuedlr/generated/Lr/Module.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "edges/generated/G/Module.hpp"
#include "engines/lr/ContinuedLineGrammar.hpp"
#include "engines/lr/PeekCaretGrammar.hpp"
#include "engines/lr/VirtualStateGrammar.hpp"
#include "indent/generated/G/Module.hpp"
#include "peekcaret/generated/G/Module.hpp"
#include "peekcaret/generated/G/Table.hpp"
#include "rulerefusal/generated/G/Module.hpp"
#include "trivialr/generated/Lr/Module.hpp"
#include "trivialr/generated/Lr/Table.hpp"
#include "wideterms/generated/G/Module.hpp"

// States a table keeps apart: peeks, virtual readings, reduce cells, and the
// states split in groups.

namespace peek_caret {

namespace g = tpeekcaret::generated::G;
// The two cells after `@`, on KEY and on RAW, both peek.
static_assert(g::kTable.npeeks == 2);

int run() {
  const auto parser = g::create_parser();
  const auto splice = tedges::generated::G::create_parser();
  constexpr cursiv::TextOffset kSpliceHead = 7;
  for (const std::string text : {"xab!", "yab?"}) {
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && r.diagnostics.empty());
    std::vector<cursiv::NextFeature> out;
    // Replace the suffix so the lexer cannot select the right answer from
    // the existing punctuation. The two-symbol Pair return must retain its
    // caller: LALR and conflict-only re-fusion offer both '!' and '?' here.
    splice->nextFeaturesAt(r.cst(), kSpliceHead + 3, kSpliceHead + 4, out);
    CHECK(out.size() == 1);
    for (const auto& f : out)
      CHECK(splice->getGrammar().terminalText(f.feature) ==
            std::string_view(text).substr(3));
  }
  for (const std::string text : {"xAb!", "yAb?", "xb!", "yb?", "x b!", "y b?"}) {
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && !r.diagnostics.empty());
    if (text[1] == ' ') {
      bool insertedBeforeTrivia = false;
      r.cst().visit_leaves([&](const cursiv::CstLeaf& leaf) {
        insertedBeforeTrivia =
            insertedBeforeTrivia ||
            (leaf.recovered && leaf.begin == kSpliceHead + 1 &&
             leaf.end == kSpliceHead + 1);
      });
      CHECK(insertedBeforeTrivia);
    }
    std::vector<cursiv::NextFeature> out;
    const auto at = static_cast<cursiv::TextOffset>(text.size() - 1);
    // The same invisible Pair return after a repaired token: the typo keeps
    // its corrected terminal, and the earlier zero-width insert is a shift.
    splice->nextFeaturesAt(r.cst(), kSpliceHead + at, kSpliceHead + at + 1,
                           out);
    CHECK(out.size() == 1);
    for (const auto& f : out)
      CHECK(splice->getGrammar().terminalText(f.feature) ==
            std::string_view(text).substr(at));
  }
  for (const std::string text : {"foo", "(fao"}) {
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && !r.diagnostics.empty());
    std::vector<cursiv::NextFeature> out;
    splice->nextFeaturesAt(r.cst(), kSpliceHead + text.size(),
                           kSpliceHead + text.size(), out);
    bool closes = false;
    for (const auto& f : out)
      closes = closes || splice->getGrammar().terminalText(f.feature) == ")";
    CHECK(out.size() == 1 && closes);
  }
  for (const std::string text : {"u(a!", "u ( a ! "}) {
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && !r.diagnostics.empty());
    std::vector<cursiv::NextFeature> out;
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    splice->nextFeaturesAt(r.cst(), kSpliceHead + at, kSpliceHead + at, out);
    bool next = false;
    for (const auto& f : out)
      next = next || splice->getGrammar().terminalText(f.feature) == "next";
    CHECK(out.size() == 1 && next);
  }
  {
    const auto r = splice->parse("splice:{ac}");
    CHECK(r.accepted && r.diagnostics.empty());
    std::vector<cursiv::NextFeature> out;
    splice->nextFeaturesAt(r.cst(), kSpliceHead + 2, kSpliceHead + 2, out);
    bool inserts = false;
    for (const auto& f : out)
      inserts = inserts || splice->getGrammar().terminalText(f.feature) == "b";
    CHECK(out.size() == 2 && inserts);
  }
  for (const auto count : {1u, 16u, 65u}) {
    const auto text = "[a" + std::string(count, 'c') + "d]";
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && r.diagnostics.empty());
    std::vector<cursiv::NextFeature> out;
    splice->nextFeaturesAt(r.cst(), kSpliceHead + 2, kSpliceHead + 2, out);
    bool tail = false;
    for (const auto& f : out) {
      CHECK(splice->getGrammar().terminalText(f.feature) != "b");
      tail = tail || splice->getGrammar().terminalName(f.feature) == "Tail";
    }
    CHECK(out.size() == 1 && tail);
  }
  {
    const auto r = splice->parse("splice:<ac>");
    CHECK(r.accepted && r.diagnostics.empty());
    const auto changed = splice->parse("splice:<abc>");
    CHECK(!changed.accepted || !changed.diagnostics.empty());
    std::vector<cursiv::NextFeature> out;
    splice->nextFeaturesAt(r.cst(), kSpliceHead + 2, kSpliceHead + 2, out);
    bool keeps = false;
    for (const auto& f : out) {
      const auto word = splice->getGrammar().terminalText(f.feature);
      CHECK(word != "b");
      keeps = keeps || word == "c";
    }
    CHECK(out.size() == 2 && keeps);
  }
  // A leaf two readings share: a candidate only one of them leads to glues
  // onto that reading, whose guard refuses it here.
  {
    const auto r = splice->parse("splice:<a");
    std::vector<cursiv::NextFeature> out;
    splice->nextFeaturesAt(r.cst(), kSpliceHead + 2, kSpliceHead + 2, out);
    bool b = false, c = false, d = false;
    for (const auto& f : out) {
      const auto word = splice->getGrammar().terminalText(f.feature);
      b = b || word == "b";
      c = c || word == "c";
      d = d || word == "d";
    }
    CHECK(!b && c && d);
  }
  for (const std::string text : {"p@tx!", "p@ty!", "q@tz?", "q@tw?"}) {
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    std::vector<cursiv::NextFeature> out;
    parser->nextFeaturesAt(r.cst(), 3, 4, out);
    bool left = false, right = false;
    for (const auto& f : out) {
      const auto word = parser->getGrammar().terminalText(f.feature);
      const bool isLeft = word == "x" || word == "z";
      const bool isRight = word == "y" || word == "w";
      if (!isLeft && !isRight)
        std::printf("unexpected '%.*s' in %s\n", int(word.size()), word.data(),
                    text.c_str());
      CHECK(isLeft || isRight);
      if (!isLeft && !isRight) continue;
      left = left || isLeft;
      right = right || isRight;
      CHECK(f.property == (isLeft ? "left" : "right"));
      CHECK(f.type == &cursiv::ast_type_tag<tpeekcaret::Doc>);
      CHECK(f.narrow == 0);
    }
    CHECK(left && right);
  }
  // The typed `t` is KEY on the tape and RAW under the words that follow it:
  // the caret offers what either reading leads to.
  for (const std::string_view text : {"p@t", "q@t"}) {
    const auto r = parser->parse(text);
    std::vector<cursiv::NextFeature> out;
    parser->nextFeaturesAt(r.cst(), 3, 3, out);
    std::vector<std::string_view> words;
    for (const auto& f : out)
      words.push_back(parser->getGrammar().terminalText(f.feature));
    std::sort(words.begin(), words.end());
    CHECK(
        (words == std::vector<std::string_view>{"#", "$", "w", "x", "y", "z"}));
  }
  return 0;
}

}  // namespace peek_caret

namespace virtual_state {

int run() {
  const auto implied = tpeekcaret::generated::G::create_parser();
  for (const std::string_view body : {"px", "px:", "px;", "px:,;", "px;,;"}) {
    const std::string text = "virtual:" + std::string(body);
    const auto r = implied->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto* root = r.value_as<tvirtual::Node>();
    CHECK(root && root->child);
    if (!root || !root->child) continue;
    const bool first = body.starts_with("px;");
    CHECK(root->child->first == first);
    CHECK(root->child->second == !first);
  }
  const auto indented = tindent::generated::G::create_parser();
  for (const std::string_view body : {"[x]\ny", "[x]\r\ny"}) {
    const std::string text = "joined virt " + std::string(body);
    const auto r = indented->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto* root = r.value_as<tvirtual::Node>();
    CHECK(root && root->first && !root->second);
  }
  return 0;
}

}  // namespace virtual_state

namespace reduce_cells {

int run() {
  const auto parser = tpeekcaret::generated::G::create_parser();
  for (const char term : {'x', 'y', 'z'}) {
    for (const unsigned distance : {0u, 64u}) {
      for (const char arm : {'a', 'b'}) {
        const std::string text = std::string("reduce:p") + term +
                                 std::string(distance, 'n') + arm + '!';
        const auto r = parser->parse(text);
        CHECK(r.accepted && r.diagnostics.empty());
        const auto* root = r.value_as<treducecells::Node>();
        CHECK(root && root->first == (arm == 'a') &&
              root->second == (arm == 'b'));
        const auto repaired = parser->parse(text.substr(0, text.size() - 1));
        CHECK(repaired.accepted && repaired.diagnostics.size() == 1);
        const auto* fixed = repaired.value_as<treducecells::Node>();
        CHECK(fixed && fixed->first == (arm == 'a') &&
              fixed->second == (arm == 'b'));
      }
    }
  }
  return 0;
}

}  // namespace reduce_cells

// A goto the head refuses lands nowhere, even across emitted state groups.
namespace group_guard {

namespace {

// Representative empty guarded readings and refusals must retain their tree.
void a_text_no_condition_admits_is_repaired_not_lost(
    const cursiv::Parser& parser, const cursiv::AstReflection& reflection) {
  const auto tree = [&](const cursiv::ParseResult& result) {
    std::string out;
    if (result.value != nullptr)
      cursiv::test::ast_of(reflection, *result.value, out);
    return out;
  };
  for (const std::string_view text :
       {"()", "(\n)", "( )", "(x)", "(@)", "(", ")"}) {
    const std::string source = "groups:" + std::string(text);
    const auto result = parser.parse(source);
    CHECK(result.accepted && result.value != nullptr);
  }
  for (const auto& [text, want] :
       {std::pair<std::string_view, std::string_view>{"()",
                                                      "Doc{mark=Slow{} }"},
        {"(\n)", "Doc{mark=Fast{} }"},
        {"( \n)", "Doc{mark=Fast{} }"}}) {
    const std::string source = "groups:" + std::string(text);
    const auto r = parser.parse(source);
    CHECK(r.accepted && r.diagnostics.empty() && !r.cst().recovered());
    CHECK(tree(r) == want);
  }
  const auto repaired = parser.parse("groups:( )");
  CHECK(repaired.value != nullptr && !repaired.diagnostics.empty() &&
        repaired.cst().recovered());
}

}  // namespace

int run() {
  const auto parser = tedges::generated::G::create_parser();
  // The blank the tight mode reads no token of reduces the empty rule.
  for (const std::string_view text : {"p a c", "p ac"}) {
    const std::string source = "groups:" + std::string(text);
    const cursiv::ParseResult refused = parser->parse(source);
    CHECK(refused.accepted && !refused.diagnostics.empty());
    const cursiv::ParseResult clean = parser->parse("groups:p ax c");
    CHECK(clean.accepted && clean.diagnostics.empty());
  }
  // A state whose one action reduces the empty rule keeps its refusal label.
  CHECK(parser->parse("groups:landing p y x").accepted);
  CHECK(parser->parse("groups:landing p y x").diagnostics.empty());
  CHECK(parser->parse("groups:landing p x").accepted);
  const auto reflection = tedges::generated::G::create_ast_reflection();
  a_text_no_condition_admits_is_repaired_not_lost(*parser, *reflection);
  return 0;
}

}  // namespace group_guard

namespace case_lrcaretkeywords {
// A state that shifts many keywords offers each of them once at a caret, and
// a completion asks the state's run for the candidate's own entries only.

namespace {

namespace g = twide::generated::G;

void every_keyword_is_offered_once() {
  static const auto parser = g::create_parser();
  for (const std::string text : {"", "k001 ", "k001 k002 k255 "}) {
    const cursiv::ParseResult r = parser->parse(text);
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    std::vector<cursiv::NextFeature> out;
    parser->nextFeaturesAt(r.cst(), at, at, out);
    std::set<std::string> seen;
    std::size_t texts = 0;
    for (const auto& f : out) {
      const std::string_view t = parser->getGrammar().terminalText(f.feature);
      if (t.empty()) continue;
      ++texts;
      seen.insert(std::string(t));
    }
    CHECK(seen.size() == 520);
    CHECK(texts == 520);
    for (unsigned k = 0; k < 520; ++k)
      CHECK(seen.contains("k" + std::string(k < 10 ? "00" : k < 100 ? "0" : "") + std::to_string(k)));
  }
  std::printf("  every keyword is offered once\n");
}

}  // namespace

int run() {
  cursiv::test::count_expectations();
  every_keyword_is_offered_once();
  return 0;
}
}  // namespace case_lrcaretkeywords

namespace case_lrcarettrivia {
// A caret inside a run of ignored blanks answers as the run's end does, and a
// caret where an ignored line comment goes on answers nothing: what is typed
// there is part of the comment.

namespace {

namespace g = tedges::generated::G;

std::set<std::string> offered(const std::string& text, std::size_t caret) {
  static const auto parser = g::create_parser();
  const std::string source = "trivia:" + text;
  const cursiv::ParseResult r = parser->parse(source);
  const auto at = static_cast<cursiv::TextOffset>(7 + caret);
  std::vector<cursiv::NextFeature> out;
  parser->nextFeaturesAt(r.cst(), at, at, out);
  std::set<std::string> words;
  for (const auto& f : out) {
    const std::string_view t = parser->getGrammar().terminalText(f.feature);
    if (!t.empty()) words.insert(std::string(t));
  }
  return words;
}

const std::set<std::string> kNext{"north", "south"};

void a_caret_inside_blanks_answers_as_their_end_does() {
  for (const std::string text : {"go      ", "go      \n"})
    for (std::size_t caret = 3; caret <= 8; ++caret)
      CHECK(offered(text, caret) == kNext);
  std::printf("  a caret inside blanks answers as their end does\n");
}

void a_caret_inside_a_continuation_answers_nothing() {
  CHECK(offered("go \\\n ", 3) == kNext);
  CHECK(offered("go \\\n ", 4).empty());
  CHECK(offered("go \\\n ", 5) == kNext);
  CHECK(offered("go \\\n ", 6) == kNext);
  std::printf("  a caret inside a continuation answers nothing\n");
}

void a_caret_in_a_line_comment_answers_nothing() {
  CHECK(offered("go // note", 10).empty());
  CHECK(offered("go // note\n", 10).empty());
  CHECK(offered("go // note", 6).empty());
  CHECK(offered("go // note\n", 6).empty());
  CHECK(offered("go // note\n", 11) == kNext);
  std::printf("  a caret in a line comment answers nothing\n");
}

}  // namespace

int run() {
  cursiv::test::count_expectations();
  a_caret_inside_blanks_answers_as_their_end_does();
  a_caret_inside_a_continuation_answers_nothing();
  a_caret_in_a_line_comment_answers_nothing();
  return 0;
}
}  // namespace case_lrcarettrivia

namespace case_lrbracketcaret {
// A grammar that declares delimiters without laying an indent or a dedent
// still counts them: the caret reads a newline between an opener and its
// closer as the blank the parse reads, whichever pair of the three it is, and
// offers what the parse would take there.

namespace {

namespace g = ttrivia::generated::Lr;
static_assert(cursiv::kLaid<g::kTable.cfg, cursiv::kLaidIndent> == 0);
static_assert(cursiv::kLaid<g::kTable.cfg, cursiv::kLaidDedent> == 0);

std::set<std::string> offered(const std::string& text, std::size_t caret) {
  static const auto parser = g::create_parser();
  const std::string source = "bracket:" + text;
  const cursiv::ParseResult r = parser->parse(source);
  const auto at = static_cast<cursiv::TextOffset>(8 + caret);
  std::vector<cursiv::NextFeature> out;
  parser->nextFeaturesAt(r.cst(), at, at, out);
  std::set<std::string> words;
  for (const auto& f : out) {
    const std::string_view t = parser->getGrammar().terminalText(f.feature);
    words.insert(std::string(
        t.empty() ? parser->getGrammar().terminalName(f.feature) : t));
  }
  return words;
}

bool parse_clean(const std::string& text) {
  static const auto parser = g::create_parser();
  const std::string source = "bracket:" + text;
  const cursiv::ParseResult r = parser->parse(source);
  return r.accepted && r.diagnostics.empty();
}

void a_newline_between_delimiters_is_a_blank_to_the_caret() {
  struct Pair {
    char open, close;
  };
  for (const Pair p : {Pair{'(', ')'}, Pair{'[', ']'}, Pair{'{', '}'}}) {
    const std::set<std::string> inside{"ID", std::string(1, p.close)};
    const std::string head = std::string("a ") + p.open + "b\n";
    CHECK(parse_clean(head + " c" + p.close + "\n"));
    CHECK(offered(head + " c" + p.close + "\n", 5) == inside);
    CHECK(offered(head, 5) == inside);
    CHECK(offered(head + "  ", 7) == inside);
  }
  CHECK(offered("a b\n", 4).count("ID") == 1);
}

}  // namespace

int run() {
  a_newline_between_delimiters_is_a_blank_to_the_caret();
  return 0;
}
}  // namespace case_lrbracketcaret

namespace case_lrcontinuedline {
int run() {
  const auto parser = tcontinued::generated::Lr::create_parser();
  for (const std::string_view text : {
           "s:\n  x ;<1\n",
           "s:\n  x ;\\\n<1\n",
           "s:\n  x ;!<1\n",
           "s:\n  x y ;\\\n<1\n  z ;\\\n  <1\n",
           "s:\n  x ;\\\n\\\n<1\n",
       }) {
    const std::string source(text);
    const auto result = parser->parse(source);
    CHECK(result.accepted && result.diagnostics.empty());
    const auto* root = result.value_as<tcontinued::ast::Node>();
    const bool multiple = text.find("x y") != std::string_view::npos;
    CHECK(root && root->body.size() == (multiple ? 2u : 1u));
    if (!root || root->body.empty()) continue;
    for (const auto* child : root->body) {
      CHECK(cursiv::ast_cast<tcontinued::ast::Less>(child));
      CHECK(child->parent == root);
    }
    CHECK(root->body[0]->name == (multiple ? "xy" : "x"));
    if (multiple && root->body.size() == 2) CHECK(root->body[1]->name == "z");
  }
  return 0;
}
}  // namespace case_lrcontinuedline

namespace case_lrconditionrefs {
// A condition over a terminal asks what the terminal's body reads, at any
// depth of the expression it stands in.

namespace {

namespace g = trefuse::head::generated::G;

void a_condition_reads_the_terminals_it_names() {
  static const auto parser = g::create_parser();
  for (const std::string text :
       {"a 12; b 7; c xyz; c abc9;", "a 1;", "c 9abc;"}) {
    const cursiv::ParseResult r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
  }
  for (const std::string text : {"a 1.5;", "c 12;"}) {
    const cursiv::ParseResult r = parser->parse(text);
    CHECK(!r.diagnostics.empty());
  }
  std::printf("  a condition reads the terminals it names\n");
}

}  // namespace

int run() {
  cursiv::test::count_expectations();
  a_condition_reads_the_terminals_it_names();
  return 0;
}
}  // namespace case_lrconditionrefs

int main() {
  int movedStatus = 0;
  movedStatus |= case_lrcaretkeywords::run();
  movedStatus |= case_lrcarettrivia::run();
  movedStatus |= case_lrbracketcaret::run();
  movedStatus |= case_lrcontinuedline::run();
  movedStatus |= case_lrconditionrefs::run();
  int status = 0;
  status |= peek_caret::run();
  status |= virtual_state::run();
  status |= reduce_cells::run();
  status |= group_guard::run();
  return (cursiv::test::report("LrStates_test") | status) | movedStatus;
}
