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
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/shared/runtime/IndentLevel.hpp"
#include "cursiv/test/AstText.hpp"
#include "cursiv/test/Check.hpp"
#include "edges/generated/G/Module.hpp"
#include "engines/lr/ContinuedLineGrammar.hpp"
#include "engines/lr/ParseChecks.hpp"
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

void run() {
  const auto parser = g::create_parser();
  const auto splice = tedges::generated::G::create_parser();
  constexpr cursiv::TextOffset kSpliceHead = 7;
  for (const std::string text : {"xab!", "yab?"}) {
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && r.diagnostics.empty());
    // Replace the suffix so the lexer cannot select the right answer from
    // the existing punctuation. The two-symbol Pair return must retain its
    // caller: LALR and conflict-only re-fusion offer both '!' and '?' here.
    const auto out =
        checks::features(*splice, r, kSpliceHead + 3, kSpliceHead + 4);
    CHECK(out.size() == 1);
    for (const auto& f : out) {
      const auto spellings = splice->getGrammar().terminalSpellings(f.feature);
      CHECK(spellings.size() == 1 &&
            spellings.front() == std::string_view(text).substr(3));
    }
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
    const auto at = static_cast<cursiv::TextOffset>(text.size() - 1);
    // The same invisible Pair return after a repaired token: the typo keeps
    // its corrected terminal, and the earlier zero-width insert is a shift.
    const auto out =
        checks::features(*splice, r, kSpliceHead + at, kSpliceHead + at + 1);
    CHECK(out.size() == 1);
    for (const auto& f : out) {
      const auto spellings = splice->getGrammar().terminalSpellings(f.feature);
      CHECK(spellings.size() == 1 &&
            spellings.front() == std::string_view(text).substr(at));
    }
  }
  for (const std::string text : {"foo", "(fao"}) {
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && !r.diagnostics.empty());
    const auto at = static_cast<cursiv::TextOffset>(kSpliceHead + text.size());
    const auto out = checks::features(*splice, r, at, at);
    CHECK(out.size() == 1 && checks::spelled(*splice, out, ")"));
  }
  for (const std::string text : {"u(a!", "u ( a ! "}) {
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && !r.diagnostics.empty());
    const auto at = static_cast<cursiv::TextOffset>(kSpliceHead + text.size());
    const auto out = checks::features(*splice, r, at, at);
    CHECK(out.size() == 1 && checks::spelled(*splice, out, "next"));
  }
  {
    const auto r = splice->parse("splice:{ac}");
    CHECK(r.accepted && r.diagnostics.empty());
    const auto out =
        checks::features(*splice, r, kSpliceHead + 2, kSpliceHead + 2);
    CHECK(out.size() == 2 && checks::spelled(*splice, out, "b"));
  }
  for (const auto count : {1u, 16u, 65u}) {
    const auto text = "[a" + std::string(count, 'c') + "d]";
    const std::string source = "splice:" + text;
    const auto r = splice->parse(source);
    CHECK(r.accepted && r.diagnostics.empty());
    const auto out =
        checks::features(*splice, r, kSpliceHead + 2, kSpliceHead + 2);
    bool tail = false;
    for (const auto& f : out) {
      for (const auto s : splice->getGrammar().terminalSpellings(f.feature))
        CHECK(s != "b");
      tail = tail || splice->getGrammar().terminalName(f.feature) == "Tail";
    }
    CHECK(out.size() == 1 && tail);
  }
  {
    const auto r = splice->parse("splice:<ac>");
    CHECK(r.accepted && r.diagnostics.empty());
    const auto changed = splice->parse("splice:<abc>");
    CHECK(!changed.accepted || !changed.diagnostics.empty());
    const auto out =
        checks::features(*splice, r, kSpliceHead + 2, kSpliceHead + 2);
    bool keeps = false;
    for (const auto& f : out)
      for (const auto word :
           splice->getGrammar().terminalSpellings(f.feature)) {
        CHECK(word != "b");
        keeps = keeps || word == "c";
      }
    CHECK(out.size() == 2 && keeps);
  }
  // A leaf two readings share: a candidate only one of them leads to glues
  // onto that reading, whose guard refuses it here.
  {
    const auto out = checks::features(*splice, splice->parse("splice:<a"),
                                      kSpliceHead + 2, kSpliceHead + 2);
    CHECK(!checks::spelled(*splice, out, "b") &&
          checks::spelled(*splice, out, "c") &&
          checks::spelled(*splice, out, "d"));
  }
  // One terminal two readings read in different modes: a candidate glued to
  // it is judged in the mode of the reading that goes on with it. The `>`
  // follows the `-` only where the mode also reads `->`, which takes it in.
  {
    const std::string text = "splice:&=-";
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    CHECK(checks::offers(*splice, text, at, at, "!") &&
          !checks::offers(*splice, text, at, at, ">"));
    const auto glued = splice->parse(text + ">");
    CHECK(!glued.accepted || !glued.diagnostics.empty());
    for (const std::string_view typed : {"!", " >"}) {
      const auto parsed = splice->parse(text + std::string(typed));
      CHECK(parsed.accepted && parsed.diagnostics.empty());
    }
  }
  // One reading through a reduction: the state that reduces the `%` reads the
  // `-` before the state that shifts it, and only the first reads `->`. A `>`
  // glued to the `-` makes it `->`, so it follows only after a blank.
  for (const std::string_view gap : {"", " "}) {
    const std::string text = "splice:%-" + std::string(gap);
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    CHECK(checks::offers(*splice, text, at, at, ">") == !gap.empty());
    const auto typed = splice->parse(text + ">");
    CHECK((typed.accepted && typed.diagnostics.empty()) == !gap.empty());
  }
  // A condition reads a run of `~` as one token, however many the arms read
  // it as, and reads on to what follows the run. Past a caret on its first
  // token it reads the document's text; before a caret in the run it reads
  // what is typed there, and a `?` makes it refuse the `^` it stands before.
  {
    const auto offered = [&](const std::string& text, cursiv::TextOffset at,
                             cursiv::TextOffset resume) {
      std::set<std::string_view> words;
      for (const auto& f :
           checks::features(*splice, splice->parse(text), at, resume)) {
        const auto spellings =
            splice->getGrammar().terminalSpellings(f.feature);
        if (spellings.empty()) words.insert(std::string_view{});
        words.insert(spellings.begin(), spellings.end());
      }
      return words;
    };
    const auto clean = [&](const std::string& text) {
      const auto r = splice->parse(text);
      return r.accepted && r.diagnostics.empty();
    };
    const std::string run(40, '~');
    const std::string held = "splice:|" + run + "!";
    CHECK(clean(held) &&
          offered(held, kSpliceHead, kSpliceHead + 1).contains("|"));
    const std::string refused = "splice:^" + run + "?";
    CHECK(!clean(refused) &&
          !offered(refused, kSpliceHead, kSpliceHead + 1).contains("^"));
    const std::string cut = "splice:^" + run.substr(20);
    const auto at = static_cast<cursiv::TextOffset>(cut.size());
    for (const std::string& text : {cut + run.substr(20) + "!", cut}) {
      const auto words =
          offered(text, at, std::min<cursiv::TextOffset>(at + 1, text.size()));
      CHECK(words.contains("!") && !words.contains("?"));
    }
    CHECK(clean(cut + "!") && !clean(cut + "?"));
  }
  for (const std::string text : {"p@tx!", "p@ty!", "q@tz?", "q@tw?"}) {
    const auto r = parser->parse(text);
    CHECK(r.accepted && r.diagnostics.empty());
    bool left = false, right = false;
    for (const auto& f : checks::features(*parser, r, 3, 4)) {
      const auto spellings = parser->getGrammar().terminalSpellings(f.feature);
      const std::string_view word =
          spellings.size() == 1 ? spellings.front() : std::string_view{};
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
    std::vector<std::string_view> words;
    for (const auto& f : checks::features(*parser, parser->parse(text), 3, 3)) {
      const auto spellings = parser->getGrammar().terminalSpellings(f.feature);
      if (spellings.empty()) words.emplace_back();
      words.insert(words.end(), spellings.begin(), spellings.end());
    }
    std::sort(words.begin(), words.end());
    CHECK(
        (words == std::vector<std::string_view>{"#", "$", "w", "x", "y", "z"}));
  }
}

}  // namespace peek_caret

namespace virtual_state {

void run() {
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
}

}  // namespace virtual_state

namespace reduce_cells {

void run() {
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

void run() {
  const auto parser = tedges::generated::G::create_parser();
  // The blank the tight mode reads no token of reduces the empty rule.
  for (const std::string_view text : {"p a c", "p ac"}) {
    const std::string source = "groups:" + std::string(text);
    const cursiv::ParseResult refused = parser->parse(source);
    CHECK(refused.accepted && !refused.diagnostics.empty());
    const cursiv::ParseResult clean = parser->parse("groups:p axc");
    CHECK(clean.accepted && clean.diagnostics.empty());
  }
  // A state whose one action reduces the empty rule keeps its refusal label.
  CHECK(parser->parse("groups:landing p y x").accepted);
  CHECK(parser->parse("groups:landing p y x").diagnostics.empty());
  CHECK(parser->parse("groups:landing p x").accepted);
  const auto reflection = tedges::generated::G::create_ast_reflection();
  a_text_no_condition_admits_is_repaired_not_lost(*parser, *reflection);
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
    const auto at = static_cast<cursiv::TextOffset>(text.size());
    std::set<std::string> seen;
    std::size_t texts = 0;
    for (const auto& f : checks::features(*parser, parser->parse(text), at, at))
      for (const std::string_view t :
           parser->getGrammar().terminalSpellings(f.feature)) {
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

void run() {
  every_keyword_is_offered_once();
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
  return checks::spellings(*parser, "trivia:" + text,
                           static_cast<cursiv::TextOffset>(7 + caret));
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

void run() {
  a_caret_inside_blanks_answers_as_their_end_does();
  a_caret_inside_a_continuation_answers_nothing();
  a_caret_in_a_line_comment_answers_nothing();
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
  const auto at = static_cast<cursiv::TextOffset>(8 + caret);
  std::set<std::string> words;
  for (const auto& f :
       checks::features(*parser, parser->parse("bracket:" + text), at, at)) {
    const auto spellings = parser->getGrammar().terminalSpellings(f.feature);
    words.insert(std::string(
        spellings.size() == 1 ? spellings.front()
                              : parser->getGrammar().terminalName(f.feature)));
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

void run() {
  a_newline_between_delimiters_is_a_blank_to_the_caret();
}
}  // namespace case_lrbracketcaret

namespace case_lrcontinuedline {
void run() {
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

void run() {
  a_condition_reads_the_terminals_it_names();
}
}  // namespace case_lrconditionrefs

void run_lr_rule_refusal_cases();

int main() {
  run_lr_rule_refusal_cases();
  case_lrcaretkeywords::run();
  case_lrcarettrivia::run();
  case_lrbracketcaret::run();
  case_lrcontinuedline::run();
  case_lrconditionrefs::run();
  peek_caret::run();
  virtual_state::run();
  reduce_cells::run();
  group_guard::run();
  return cursiv::test::report("LrStates_test");
}
