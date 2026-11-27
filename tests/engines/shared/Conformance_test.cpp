// The conformance cases of the grammar reference.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "conformancelr/generated/Suite/Module.hpp"
#include "conformancelr/generated/Suite/Table.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Parse.hpp"
#include "cursiv/engine/parser/Split.hpp"
#include "cursiv/engine/recovery/Repair.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/CaretOffers.hpp"
#include "engines/shared/Checks.hpp"
#include "engines/shared/ConformanceGrammars.hpp"
#include "engines/shared/ConformanceTest.hpp"
namespace condition_contract_cases {
void run();
}
namespace template_tail_cases {
void run();
}
namespace section_predicate_cases {
void run();
}
namespace conformance_section_close_cases {
void run();
}
namespace branch_values_cases {
void run();
}

namespace {

// Unoptimised code runs the same work many times slower.
#ifdef NDEBUG
constexpr int kSlowdown = 1;
#else
constexpr int kSlowdown = 10;
#endif

using conformance_test::clean;
using conformance_test::expect;
using conformance_test::Entry;

template <class T, class Check>
void tree(const Entry& entry, std::string_view text, Check&& check,
          std::source_location at = std::source_location::current()) {
  const std::string source = entry.source(text);
  const auto r = conformance_test::parser().parse(source);
  const auto* node = exactly<T>(r);
  const bool ok = clean(r) && node != nullptr && check(*node);
  if (!ok)
    std::printf("[%.*s]: wrong tree (row %u)\n", static_cast<int>(text.size()),
                text.data(), at.line());
  CHECK(ok);
}

// The two texts a `Won` or a `Pick` holds.
void won(const Entry& entry, std::string_view text, std::string_view token,
         std::string_view name,
         std::source_location at = std::source_location::current()) {
  tree<tconformance::ast::Won>(
      entry, text,
      [&](const auto& n) { return n.token == token && n.name == name; }, at);
}
void pick(const Entry& entry, std::string_view text, std::string_view literal,
          std::string_view keyword,
          std::source_location at = std::source_location::current()) {
  tree<tconformance::ast::Pick>(
      entry, text,
      [&](const auto& n) {
        return n.literal == literal && n.keyword == keyword;
      },
      at);
}

// The steps the states take ahead to read `text`, or to repair it once they
// refuse it.
std::uint64_t steps(std::string_view text) {
  namespace engine = cursiv::engine;
  cursiv::Cst cst(text);
  cursiv::CstBuilder builder(cst);
  engine::Machine m(tconformance::generated::Suite::kTables, cst.text(), builder.stream(),
                    cursiv::kNoCancel);
  engine::reserve_for(builder.stream(), text.size());
  if (engine::run(m) == engine::Exit::accepted) return m.explored;
  const auto before = m.explored;
  CHECK(engine::repair(m, builder));
  return m.explored - before;
}

// A chain of `count` suffixes, the name of suffix `left` left out.
std::string chain_without(int count, int left) {
  std::string text = "chain:a";
  for (int i = 0; i < count; ++i) text += i == left ? "." : ".b";
  return text;
}

}  // namespace

int main() {
  namespace ast = tconformance::ast;
  cursiv::test::countExpectations();

  const Entry keyword{"keyword:"};
  expect(keyword, "if", true);
  expect(keyword, "ifx", false);

  // A keyword and a literal of one spelling tie: the arm written first wins.
  const Entry keywordTie{"keywordtie:"};
  pick(keywordTie, "if(", "if", "");
  const Entry keywordTieBoundary{"keywordtieboundary:"};
  pick(keywordTieBoundary, "if x", "if", "");
  pick(keywordTieBoundary, "ifx", "if", "");
  const Entry firstArm{"firstarm:"};
  tree<ast::First>(firstArm, "if then", [](const ast::First&) { return true; });
  // A misspelt word after the shared head and a name of one part or more
  // leaves every statement open: it is read as the keyword it misspells, of
  // the second as of the first.
  const Entry sharedHead{"sharedhead:"};
  won(sharedHead, "ev a.b early", "a.b", "");
  won(sharedHead, "ev a.b late", "", "a.b");
  for (const auto& [text, token, name] :
       {std::tuple{"ev a.b eirly", "a.b", ""}, std::tuple{"ev a.b lite", "", "a.b"},
        std::tuple{"ev a eirly", "a", ""}, std::tuple{"ev a lite", "", "a"}}) {
    const std::string source = sharedHead.source(text);
    const auto r = conformance_test::parser().parse(source);
    const auto* node = exactly<ast::Won>(r);
    CHECK(r.accepted && node != nullptr && node->token == token && node->name == name);
    CHECK(r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced &&
          r.diagnostics[0].end == source.size());
  }
  // With names allowed before that word, its misspelling reads as one more
  // name and the end of the text is refused: the word is read as the keyword
  // by the statement that takes it.
  const Entry sharedTags{"sharedtags:"};
  won(sharedTags, "ev a.b x early", "a.b", "");
  won(sharedTags, "ev a.b late", "", "a.b");
  tree<ast::First>(sharedTags, "ev never", [](const ast::First&) { return true; });
  for (const auto& [text, token, name] :
       {std::tuple{"ev a.b x eirly", "a.b", ""}, std::tuple{"ev a.b x lste", "", "a.b"}}) {
    const std::string source = sharedTags.source(text);
    const auto r = conformance_test::parser().parse(source);
    const auto* node = exactly<ast::Won>(r);
    CHECK(r.accepted && node != nullptr && node->token == token && node->name == name);
    CHECK(r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced &&
          r.diagnostics[0].end == source.size());
  }
  // A name that reads as that word misspelt, before the word itself: the
  // first statement reads `late` as a name, its own reading, and the keyword
  // it waits for is inserted at the end.
  {
    const std::string source = sharedTags.source("ev a.b eirly x late late");
    const auto r = conformance_test::parser().parse(source);
    const auto* node = exactly<ast::Won>(r);
    CHECK(r.accepted && node != nullptr && node->token == "a.b" && node->name.empty());
    CHECK(r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Inserted &&
          r.diagnostics[0].begin == source.size());
  }
  // The misspelt keyword of the third reads as the name the two others
  // begin with: no statement is chosen when it is refused.
  {
    const std::string source = sharedTags.source("ev nevre");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && exactly<ast::First>(r) != nullptr);
    CHECK(r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced &&
          r.diagnostics[0].end == source.size());
  }
  // Two statements read a text alike up to the token refused past it, a
  // third another way: the second reads it whole once a name is read as the
  // keyword it misspells.
  const Entry convergedArms{"convergedarms:"};
  won(convergedArms, "ev a b ;", "a", "");
  won(convergedArms, "ev a upto b ; c end ;", "", "a");
  tree<ast::First>(convergedArms, "ev a b ; done", [](const ast::First&) { return true; });
  {
    const std::string source = convergedArms.source("ev a x upxo b ; c end ;");
    const auto r = conformance_test::parser().parse(source);
    const auto* node = exactly<ast::Won>(r);
    CHECK(r.accepted && node != nullptr && node->token.empty() && node->name == "a");
    CHECK(r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced);
  }
  // A name that reads as `done` misspelt, past the split the statements part
  // at: the text cut before it leaves the first statement there, which does
  // not read up to the token refused, so the states read on to it again.
  {
    const std::string source = convergedArms.source("ev a b dnoe upto ; d e f end ; g");
    const auto r = conformance_test::parser().parse(source);
    const auto* node = exactly<ast::Won>(r);
    CHECK(r.accepted && node != nullptr && node->token.empty() && node->name == "a");
    CHECK(r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Deleted &&
          r.diagnostics[0].end == source.size());
  }
  // Two lists share their names up to their last word, each name read two
  // ways alike: what a caret at their end is offered costs their length.
  {
    const Entry ambiguousList{"ambiguouslist:"};
    tree<ast::Wons>(ambiguousList, "a b early", [](const ast::Wons& n) { return n.all.size() == 2; });
    tree<ast::Wons>(ambiguousList, "a b ! late", [](const ast::Wons& n) { return n.all.size() == 2; });
    std::string names;
    for (int i = 0; i < 8192; ++i) names += "x ";
    const std::string source = ambiguousList.source(names);
    const auto begin = std::chrono::steady_clock::now();
    const auto offers = caret_offers::offered(conformance_test::parser(), source,
                                              static_cast<cursiv::TextOffset>(source.size()));
    CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(8 * kSlowdown));
    CHECK((offers == std::vector<std::string>{"!", "ID", "early", "late"}));
  }
  // In a chain whose readings part at the first suffix, a repair that takes
  // an action at each split before a name left out reads no text ahead at
  // them again as it writes its path.
  CHECK(steps(chain_without(40, 20)) < 17500);
  // Each level reads its block by either of two rules and goes on alike: the
  // first rule is elected at each level, and the readings that part there
  // meet again, so a level costs no more than the levels it closes.
  {
    const Entry levels{"levels:"};
    const auto deep = [](int depth) {
      std::string text;
      for (int i = 0; i < depth; ++i) text += "r {} ";
      return text + "r e";
    };
    tree<ast::Level>(levels, deep(4), [](const ast::Level& n) {
      int firsts = 0;
      const ast::Level* at = &n;
      for (; at->next != nullptr; at = at->next)
        firsts += at->first != nullptr && at->second == nullptr;
      return firsts == 4 && at->first == nullptr && at->second == nullptr;
    });
    CHECK(steps(levels.source(deep(16))) < 5 * steps(levels.source(deep(8))));
  }
  {
    const Entry split{"splitline:"};
    tree<ast::Stated>(split, "a\nb", [](const ast::Stated& n) { return n.body.size() == 1; });
    expect(split, "a b", false);
  }
  // A text behind a many opens before its first token the levels it closes:
  // reading it costs its length, and a repair or a caret there comes to an end.
  {
    const Entry behind{"behind:"};
    tree<ast::Stated>(behind, "c c a d d d", [](const ast::Stated& n) {
      return n.body.size() == 3 && n.body[0]->name == "c" && n.body[1]->name == "c" &&
             n.body[2]->body.size() == 1 && n.body[2]->body[0]->body.size() == 1 &&
             n.body[2]->body[0]->body[0]->name == "a";
    });
    const auto closed = [&](int levels, std::string_view after = "") {
      std::string text = "c a";
      for (int i = 0; i < levels; ++i) text += " d";
      text += after;
      return behind.source(text);
    };
    CHECK(steps(closed(16)) < 3 * steps(closed(8)));
    const auto extra = closed(2, " e");
    const auto repaired = conformance_test::parser().parse(extra);
    CHECK(repaired.accepted && repaired.diagnostics.size() == 1 &&
          repaired.diagnostics[0].kind == cursiv::DiagnosticKind::Deleted &&
          repaired.diagnostics[0].begin == extra.size() - 1 && repaired.diagnostics[0].end == extra.size());
    const std::string source = behind.source("c a");
    CHECK((caret_offers::offered(conformance_test::parser(), source,
                                 static_cast<cursiv::TextOffset>(source.size())) ==
           std::vector<std::string>{"d"}));
  }
  {
    const Entry guarded{"blankprefix:"};
    const std::string source = guarded.source(" a d d");
    const auto result = conformance_test::parser().parse(source);
    const auto* at = exactly<ast::Stated>(result);
    CHECK(clean(result) && at);
    for (int level = 0; level < 2 && at; ++level) {
      CHECK(at->body.size() == 3);
      if (at->body.size() != 3) break;
      CHECK(!cursiv::ast_cast<ast::SecondPrefix>(at->body[0]) &&
            cursiv::ast_cast<ast::SecondPrefix>(at->body[1]));
      for (int i = 0; i < 2; ++i) {
        const auto& prefix = result.cst()[at->body[i]->cstNodeId];
        CHECK(prefix.begin == source.rfind('a') && prefix.end == prefix.begin &&
              at->body[i]->cstNodeId < at->body[i + 1]->cstNodeId);
      }
      at = at->body[2];
    }
    CHECK(at && at->name == "a");
    expect(guarded, "b", true);
    expect(guarded, "c a d", true);
    expect(guarded, "b d", false);
    conformance_test::one_tree(guarded, "a d d e");
  }
  // Omitted recursive prefixes terminate without outranking ordinary actions.
  {
    const Entry implied{"impliedprefix:"};
    const auto source = implied.source("a");
    cursiv::Cst cst(source);
    cursiv::CstBuilder builder(cst);
    cursiv::engine::Machine machine(tconformance::generated::Suite::kTables,
                                    source, builder.stream(),
                                    cursiv::kNoCancel);
    CHECK(cursiv::engine::run(machine) == cursiv::engine::Exit::refused);
    CHECK(machine.explored < 512);
    for (const auto text : {"a", "ab", " a", ";a", ";\na"})
      expect(implied, text, false);
    for (const auto text : {"\na", ";\nab", ";;\nabb"})
      expect(implied, text, true);
    const Entry direct{"implieddirect:"};
    expect(direct, "a", true);
    expect(direct, "ab", false);
    expect(direct, ";ab", true);
    const Entry nullable{"impliednullable:"};
    for (const auto text : {"", "q", "b", ";b", ";;bb"})
      expect(nullable, text, true);
    for (const auto text : {"bb", "qb", " b", ";bb"})
      expect(nullable, text, false);
    const Entry priority{"impliedpriority:"};
    expect(priority, "bx", true);
    expect(priority, "bz", false);
    expect(priority, ";bz", true);
    const Entry reduction{"impliedreduce:"};
    expect(reduction, "bx", true);
    expect(reduction, "bz", false);
    expect(reduction, ";bz", true);
    const Entry repeated{"impliedrepeated:"};
    const auto closed = [&](unsigned count) {
      std::string text = "ab";
      for (unsigned i = 0; i < count; ++i) text += "cb";
      return text;
    };
    expect(repeated, closed(64), true);
    CHECK(steps(repeated.source(closed(64))) <
          3 * steps(repeated.source(closed(32))));
    const Entry expression{"impliedexpression:"};
    const auto operand = [](const ast::Expression* node, std::string_view op) {
      const auto name = [](const ast::Expression* child) {
        const auto* n = cursiv::ast_cast<ast::Name>(child);
        return n && n->name == "x";
      };
      if (op.empty()) return name(node);
      const auto* binary = cursiv::ast_cast<ast::Binary>(node);
      return binary && binary->op == op && name(binary->left) &&
             name(binary->right);
    };
    for (const auto& [text, op, left, right] :
         {std::tuple{"x+x*x", "+", "", "*"}, std::tuple{"x*x+x", "+", "*", ""},
          std::tuple{"x+x+x", "+", "+", ""}, std::tuple{"x^x^x", "^", "", "^"},
          std::tuple{";xb+x", "+", "", ""}}) {
      tree<ast::Binary>(expression, text, [&](const ast::Binary& node) {
        return node.op == op && operand(node.left, left) &&
               operand(node.right, right);
      });
    }
    expect(expression, "x<x<x", false);
    const Entry shared{"impliedshared:"};
    for (const auto text : {"pccccccccx*x+x!q", "rccccccccx*x+x!s"})
      tree<ast::Program>(shared, text, [&](const ast::Program& node) {
        if (node.statements.size() != 1) return false;
        const auto* binary =
            cursiv::ast_cast<ast::Binary>(node.statements.front());
        return binary && binary->op == "+" && operand(binary->left, "*") &&
               operand(binary->right, "");
      });
  }
  // A name is no number and no capitalised word, whatever its length; one
  // that only begins as either is a name.
  const Entry unboundedName{"unboundedname:"};
  for (const char* text : {"abc", "Abc9", "12x"})
    tree<ast::List>(unboundedName, text, [&](const ast::List& n) {
      return n.items.size() == 1 && n.items[0] == text;
    });
  tree<ast::List>(unboundedName, "abc 12x Abc9",
                  [](const ast::List& n) { return n.items.size() == 3; });
  for (const char* text : {"12", "Abc", "abc 12"}) {
    const std::string source = unboundedName.source(text);
    CHECK(!clean(conformance_test::parser().parse(source)));
  }
  // A keyword with a letter left out or written wrong is read as itself,
  // though other keywords of the same first letter come before it.
  const Entry faultEdges{"faultedges:"};
  tree<ast::First>(faultEdges, "i64", [](const ast::First&) { return true; });
  tree<ast::Second>(faultEdges, "include", [](const ast::Second&) { return true; });
  for (const char* text : {"iclude", "ixclude"}) {
    const std::string source = faultEdges.source(text);
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && exactly<ast::Second>(r) != nullptr);
    CHECK(r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced &&
          r.diagnostics[0].end == source.size());
  }
  // A rule whose test names a keyword and leads to a state that only reduces
  // reads a misspelt keyword as the keyword: one replacement mends `a thn b`.
  const Entry namedAhead{"namedahead:"};
  tree<ast::Doc>(namedAhead, "a then b", [](const ast::Doc&) { return true; });
  {
    const std::string source = namedAhead.source("a thn b");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced);
  }
  // A literal left out before a rule whose test names it is inserted there,
  // and read where the rule after the test reads it.
  const Entry insertedAhead{"insertedahead:"};
  tree<ast::Doc>(insertedAhead, "a . b;", [](const ast::Doc&) { return true; });
  {
    const std::string source = insertedAhead.source("a b;");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Inserted);
  }
  // Two statements on one line of a block are parted by a semicolon, which
  // costs no more than a brace that would close the block early.
  const Entry braced{"braced:"};
  const auto inserted = [](const cursiv::ParseDiagnostic& d) {
    const auto spellings = conformance_test::parser().getGrammar().terminalSpellings(
        static_cast<std::uint16_t>(d.element));
    return d.kind == cursiv::DiagnosticKind::Inserted && !spellings.empty()
               ? spellings.front()
               : std::string_view{};
  };
  tree<ast::Doc>(braced, "{ a\n go b }", [](const ast::Doc&) { return true; });
  {
    const std::string source = braced.source("{ a b\n  c\n  d e\n}");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.diagnostics.size() == 2 && inserted(r.diagnostics[0]) == ";" &&
          inserted(r.diagnostics[1]) == ";");
  }
  // A block the text leaves open is closed at its end: no repair moves the
  // statements written in it out of it.
  {
    const std::string source = braced.source("{\n  go a\n  b");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.diagnostics.size() == 1 && inserted(r.diagnostics[0]) == "}" &&
          r.diagnostics[0].begin == source.size());
  }
  // A value past a field's last part is dropped: a field invented before it
  // completes nothing of the text, so its tokens are not free, and the search
  // does not wander through every field it could invent.
  {
    const Entry slots{"slots:"};
    const std::string source = slots.source("{ o ka kb kc kd a optional = \"x\" }");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.value != nullptr && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind != cursiv::DiagnosticKind::Bounded &&
          r.diagnostics[0].begin == source.find('='));
  }
  // A test past the token in hand reads an inserted token as itself: the text
  // cut after the postfix name is closed.
  {
    const Entry postfixed{"postfixed:"};
    tree<ast::Doc>(postfixed, "(a b);", [](const ast::Doc&) { return true; });
    const std::string source = postfixed.source("(a b");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.value != nullptr && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Inserted);
  }
  // Thirty tokens missing deep in a production of nine hundred symbols are
  // one run of insertions, cheaper than three apart in the other branch,
  // though more than five hundred tokens still complete the text.
  {
    const Entry longRun{"longrun:"};
    std::string text;
    for (int i = 0; i < 270; ++i) text += "x ";
    text += ";";
    for (int i = 0; i < 600; ++i) text += " n";
    const std::string source = longRun.source(text);
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.value != nullptr && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Inserted);
  }
  // A comment left open at the end is read as one and the brace inserted: a
  // name invented before it would leave more to insert, at the end or on the
  // next lines. However long, and over however many lines, the comment is
  // walked once, whatever mode reaches it.
  {
    const Entry divided{"divided:"};
    std::string lines = "{ a ; /* b";
    for (int i = 0; i < 1 << 18; ++i) lines += "\n}";
    for (const std::string& text : {std::string("{ a ; /* b"), std::string("{ a ; /* b\nb\nb"),
                                    "{ a ; /* " + std::string(1 << 20, 'b'), lines}) {
      const std::string source = divided.source(text);
      const auto r = conformance_test::parser().parse(source);
      CHECK(r.accepted && r.value != nullptr && r.diagnostics.size() == 1 &&
            r.diagnostics[0].begin == source.find("/*"));
    }
  }
  const Entry elected{"elected:"};
  tree<ast::First>(elected, "1 if;", [](const ast::First&) { return true; });
  tree<ast::Second>(elected, "1 if:", [](const ast::Second&) { return true; });
  won(elected, "2 if", "if", "");
  won(elected, "2 iff", "", "iff");
  won(elected, "3 if(", "if", "");
  won(elected, "3 if (", "", "if");
  tree<ast::Tally>(elected, "4 if if", [](const ast::Tally& n) {
    return n.turns.size() == 2 && !n.tail;
  });
  // A token a repair inserts is the terminal it names, though others read the
  // same text: the text cut after `1` ends with one.
  {
    const std::string source = elected.source("1");
    const auto r = conformance_test::parser().parse(source);
    CHECK(r.accepted && r.value != nullptr && r.diagnostics.size() == 1 &&
          r.diagnostics[0].kind == cursiv::DiagnosticKind::Inserted);
  }
  // The election comes first: its refused conversion is reported and no other
  // reading is tried.
  {
    const std::string source = elected.source("5 if");
    const auto r = conformance_test::parser().parse(source);
    const auto* node = exactly<ast::Kept>(r);
    const bool ok =
        node != nullptr && node->value == "kept" && r.diagnostics.size() == 1 &&
        r.diagnostics[0].kind == cursiv::DiagnosticKind::ConversionError;
    if (!ok) std::printf("[5 if]: wrong conversion\n");
    CHECK(ok);
  }

  const Entry literalsRank{"literalsrank:"};
  won(literalsRank, "if", "if", "");
  won(literalsRank, "while", "while", "");
  won(literalsRank, "iff", "", "iff");
  const Entry literalsRankRefuses{"literalsrankrefuses:"};
  expect(literalsRankRefuses, "if", false);
  expect(literalsRankRefuses, "if;", true);
  expect(literalsRankRefuses, "x", true);

  const Entry literalConditionRank{"literalconditionrank:"};
  won(literalConditionRank, "if;", "if", "");
  won(literalConditionRank, "if ;", "", "if");
  won(literalConditionRank, "x;", "", "x");
  const Entry endRank{"endrank:"};
  won(endRank, "if", "if", "");
  won(endRank, "iff", "", "iff");

  const Entry mixedRank{"mixedrank:"};
  pick(mixedRank, "if(", "if", "");
  pick(mixedRank, "x(", "", "x");
  expect(mixedRank, ":{", true);
  expect(mixedRank, ":A", true);
  {
    const auto source = mixedRank.source(":(");
    const auto result = conformance_test::parser().parse(source);
    const auto at = source.find('(');
    CHECK(result.accepted && result.diagnostics.size() == 1 &&
          result.diagnostics[0].kind == cursiv::DiagnosticKind::Replaced &&
          result.diagnostics[0].begin == at && result.diagnostics[0].end == at + 1);
    result.cst().visitLeaves([&](const cursiv::CstLeaf& leaf) {
      if (leaf.recovered) CHECK(leaf.begin == at && leaf.end == at + 1);
    });
  }

  const Entry conditionedRank{"conditionedrank:"};
  pick(conditionedRank, "if;", "if", "");
  pick(conditionedRank, "if:", "if", "");
  const Entry conditionedSwapped{"conditionedrankswapped:"};
  pick(conditionedSwapped, "if;", "", "if");
  pick(conditionedSwapped, "if:", "", "if");

  const Entry patternsTie{"patternstie:"};
  won(patternsTie, "if", "if", "");
  const Entry patternsTieSwapped{"patternstieswapped:"};
  won(patternsTieSwapped, "if", "", "if");

  const Entry mixedRefused{"mixedrefused:"};
  for (const std::string_view lead : {"", "<", ">", "="}) {
    won(mixedRefused, std::string(lead) + "if;", "if", "");
    won(mixedRefused, std::string(lead) + "x;", "", "x");
  }

  const Entry mixedLeading{"mixedleading:"};
  won(mixedLeading, "if;", "if", "");
  won(mixedLeading, "x;", "", "x");

  const Entry keywordTrivia{"keywordtrivia:"};
  won(keywordTrivia, "if", "", "if");
  won(keywordTrivia, "iff", "", "iff");
  const Entry literalTrivia{"literaltrivia:"};
  won(literalTrivia, "if", "", "if");
  won(literalTrivia, "iff", "", "iff");
  const Entry conditionedTrivia{"conditionedtrivia:"};
  won(conditionedTrivia, "if;", "", "if");
  const Entry guardedLiteral{"guardedliteral:"};
  won(guardedLiteral, "if;", "if", "");
  won(guardedLiteral, "if(", "", "if");
  const Entry fallbackKeyword{"fallbackkeyword:"};
  expect(fallbackKeyword, "x/**/", true);
  expect(fallbackKeyword, "x/", true);
  const Entry literalBesideGuard{"literalbesideguard:"};
  won(literalBesideGuard, "if", "if", "");
  won(literalBesideGuard, "iff", "", "iff");
  // The lexer tries what the readings take, not what the state merged from
  // its callers would: `TEXT` is no competitor of `.` where a path is a key.
  const Entry mergedLookahead{"mergedlookahead:"};
  expect(mergedLookahead, "a.b", true);
  expect(mergedLookahead, "a.b: [@c.d e.f]", true);
  const Entry expectedKeyword{"expectedkeyword:"};
  won(expectedKeyword, "REM ; REM junk\nabc", "REM", "abc");
  // ...beyond ASCII too, where `ÿ` and `Ÿ` start on different bytes.
  const Entry foldedKeyword{"foldedkeyword:"};
  won(foldedKeyword, "HAŸ ; haÿ junk\nabc", "HAŸ", "abc");
  won(foldedKeyword, "Haÿ ; abc", "Haÿ", "abc");
  expect(foldedKeyword, "haÿ ; HAŸ", false);
  expect(foldedKeyword, "HAY ; abc", false);

  const Entry arms{"arms:"};
  const Entry guarded{"caretguards:"};
  const auto guarded_offers = [&](std::string_view text, cursiv::TextOffset resume) {
    const auto& parser = conformance_test::parser();
    const std::string source = guarded.source(text);
    const auto parsed = parser.parse(source);
    CHECK(clean(parsed));
    const auto prefix = static_cast<cursiv::TextOffset>(guarded.prefix.size());
    std::vector<cursiv::NextFeature> next;
    parser.nextFeaturesAt(parsed.cst(), prefix + 1, prefix + resume, next);
    std::vector<std::string_view> out;
    for (const auto& f : next) {
      if (!f.property.empty()) out.push_back(f.property);
      else for (auto word : parser.getGrammar().terminalSpellings(f.feature)) out.push_back(word);
    }
    std::ranges::sort(out);
    return out;
  };
  const std::vector<std::string_view> punctuation{"!!", ";"};
  CHECK(guarded_offers("x;", 2) == punctuation);
  CHECK(guarded_offers("x!!", 3) == punctuation);
  CHECK((guarded_offers("p", 1) == std::vector<std::string_view>{"name"}));
  tree<ast::Won>(guarded, "p!b+", [](const ast::Won& n) { return n.name == "!b+"; });
  CHECK((guarded_offers("@12;", 3) == std::vector<std::string_view>{"token"}));
  CHECK((guarded_offers("@aa:", 3) == std::vector<std::string_view>{"name", "token"}));
  tree<ast::Arms>(arms, "abc:", [](const ast::Arms& n) {
    return n.name == "abc" && n.low.empty();
  });
  tree<ast::Arms>(arms, "abc;", [](const ast::Arms& n) {
    return n.low == "abc" && n.name.empty();
  });
  // Each clause: conjunction, modifier, field, operator, value.
  const Entry fields{"fields:"};
  using Row = std::array<std::string_view, 5>;
  const auto query = [&](std::string_view text, std::vector<Row> rows,
                         std::source_location at =
                             std::source_location::current()) {
    tree<ast::Query>(
        fields, text,
        [&](const ast::Query& n) {
          if (n.clauses.size() != rows.size()) return false;
          for (std::size_t k = 0; k < rows.size(); ++k) {
            const ast::Clause& c = *n.clauses[k];
            if (Row{c.conjunction, c.modifier, c.field, c.op, c.value} !=
                rows[k])
              return false;
          }
          return true;
        },
        at);
  };
  query("foo*", {{"", "", "", "", "foo*"}});
  query("foo:bar", {{"", "", "foo", ":", "bar"}});
  query("age<18", {{"", "", "age", "<", "18"}});
  query("a OR b AND c", {{"", "", "", "", "a"},
                         {"OR", "", "", "", "b"},
                         {"AND", "", "", "", "c"}});
  query("+foo -bar", {{"", "+", "", "", "foo"}, {"", "-", "", "", "bar"}});
  expect(fields, "18:x", false);

  // A branch, or an optional item, that reads what an earlier one reads is
  // dropped: each case reads as the rule written without it.
  const Entry twins{"twins:"};
  const auto unchanged = [&](std::string_view twin, std::string_view text) {
    const std::string with =
        twins.source(std::string(twin) + " " + std::string(text));
    const std::string without =
        twins.source(std::string(twin) + ". " + std::string(text));
    const auto a = conformance_test::parser().parse(with);
    const auto b = conformance_test::parser().parse(without);
    const auto* x = exactly<ast::Won>(a);
    const auto* y = exactly<ast::Won>(b);
    const bool ok = clean(a) && clean(b) && x != nullptr && y != nullptr &&
                    x->token == y->token && x->name == y->name;
    if (!ok)
      std::printf("[%s]: reads unlike the rule without its twin\n",
                  with.c_str());
    CHECK(ok);
  };
  for (const char* id : {"1", "2", "3", "4"}) unchanged(id, "x");
  unchanged("4", "x;");
  for (const char* text : {"", "x", "x y"}) unchanged("5", text);
  won(twins, "2 x", "x", "");
  won(twins, "3 x", "x", "");
  won(twins, "4 x", "x", "");
  won(twins, "5 x", "x", "");
  won(twins, "5 x y", "x", "y");
  won(twins, "6 x;", "x", "");
  won(twins, "6 x", "", "x");
  // A text both branches read goes to the first; the rest to the second.
  won(twins, "7 x", "x", "");
  won(twins, "7 x;", "", "x");

  const Entry separated{"separated:"};
  tree<ast::List>(separated, "",
                  [](const ast::List& n) { return n.items.empty(); });
  tree<ast::List>(separated, "a , a",
                  [](const ast::List& n) { return n.items.size() == 2; });
  expect(separated, ",", false);
  expect(separated, "a ,", false);

  const Entry trailing{"trailing:"};
  tree<ast::List>(trailing, "a,",
                  [](const ast::List& n) { return n.items.size() == 1; });
  tree<ast::List>(trailing, "a , a ,",
                  [](const ast::List& n) { return n.items.size() == 2; });
  expect(trailing, ",", false);

  const Entry grouped{"grouped:"};
  tree<ast::S>(grouped, "x y z", [](const ast::S& n) {
    return n.a != nullptr && n.a->letters.size() == 2 && n.b == nullptr &&
           n.c != nullptr;
  });
  const Entry repeated{"repeated:"};
  tree<ast::S>(repeated, "x y z", [](const ast::S& n) {
    return n.a != nullptr && n.a->letters.size() == 1 && n.b != nullptr &&
           n.b->letters.size() == 2 && n.c == nullptr;
  });

  const Entry turn{"turn:"};
  tree<ast::Turns>(turn, "aa", [](const ast::Turns& n) {
    return n.turns.size() == 1 && n.turns[0]->letters.size() == 2;
  });
  tree<ast::Turns>(turn, "aaa", [](const ast::Turns& n) {
    return n.turns.size() == 2 && n.turns[0]->letters.size() == 2 &&
           n.turns[1]->letters.size() == 1;
  });

  const Entry turnShortFirst{"turnshortfirst:"};
  tree<ast::Turns>(turnShortFirst, "aa", [](const ast::Turns& n) {
    return n.turns.size() == 2 && n.turns[0]->letters.size() == 1 &&
           n.turns[1]->letters.size() == 1;
  });

  const Entry tight{"tight:"};
  expect(tight, "[name]", true);
  expect(tight, "[ name ]", false);

  const Entry adjacent{"adjacent:"};
  expect(adjacent, ">>", true);
  expect(adjacent, "> >", false);
  const Entry attachedCall{"attachedcall:"};
  won(attachedCall, "f();", "()", "");
  won(attachedCall, "f ();", "", "()");
  won(attachedCall, " f ();", "", "()");
  const Entry attachedCallShared{"attachedcallshared:"};
  won(attachedCallShared, "f();", "()", "");
  won(attachedCallShared, "f ();", "", "()");
  won(attachedCallShared, " f ();", "", "()");
  const Entry attachedCallCalled{"attachedcallcalled:"};
  won(attachedCallCalled, "f();", "()", "");
  won(attachedCallCalled, "f ();", "", "()");
  won(attachedCallCalled, " f ();", "", "()");

  const Entry closers{"closers:"};
  expect(closers, "<a>x", true);
  expect(closers, "<a>y>x", false);

  const Entry longString{"longstring:"};
  expect(longString, "[[x]]", true);
  expect(longString, "[=[x]=]", true);
  expect(longString, "[=[x]==]", false);

  const Entry split{"split:"};
  expect(split, "ab", false);
  expect(split, "a b", true);
  expect(split, "ab c", true);

  // `#(` ends the call loop only where the caller in hand reads it.
  const Entry longer{"callerlonger:"};
  const auto named = [](const ast::Expression* e, std::string_view name) {
    return e != nullptr && e->type == &cursiv::kAstTypeTag<ast::Name> &&
           static_cast<const ast::Name*>(e)->name == name;
  };
  const auto called = [&](const ast::Expression* e, std::string_view callee,
                          std::string_view argument) {
    if (e == nullptr || e->type != &cursiv::kAstTypeTag<ast::Call>)
      return false;
    const auto* call = static_cast<const ast::Call*>(e);
    return named(call->callee, callee) && named(call->argument, argument);
  };
  for (const std::string_view text : {"a#(b)", "a #(b)", "a# (b)"})
    tree<ast::Program>(longer, text, [&](const ast::Program& n) {
      return n.statements.size() == 1 && called(n.statements[0], "a", "b");
    });
  tree<ast::Program>(longer, "?a b#(c)", [&](const ast::Program& n) {
    return n.statements.size() == 2 && named(n.statements[0], "a") &&
           called(n.statements[1], "b", "c");
  });
  tree<ast::Program>(longer, "?a #(b)", [&](const ast::Program& n) {
    return n.statements.size() == 2 && named(n.statements[0], "a") &&
           named(n.statements[1], "b");
  });

  // Each parse reads its own token: `ab` is one where `ab` stands next, two
  // where `a` does. The second takes at most two `xa`.
  const Entry many{"callermany:"};
  const Entry upToTwo{"uptotwo:"};
  for (const Entry* shared : {&many, &upToTwo})
    for (const std::string_view text : {"xab", "xaxab", "xa xab", "xa xab y"})
      expect(*shared, text, true);
  for (const std::string_view text : {"xaxa xab", "xaxaxab"}) {
    expect(many, text, true);
    expect(upToTwo, text, false);
  }
  // Both parses read the whole text: where their tokens differ, the lexer's
  // choice elects, whatever the grammar's order.
  tree<ast::Second>(Entry{"twotokens:"}, "x q ab", [](const ast::Second&) { return true; });
  tree<ast::Second>(Entry{"twotokensswapped:"}, "x q ab", [](const ast::Second&) { return true; });
  tree<ast::First>(Entry{"keywordorname:"}, "x q if", [](const ast::First&) { return true; });
  tree<ast::Second>(Entry{"keywordorname:"}, "x q of", [](const ast::Second&) { return true; });
  // Tokens that begin apart are no choice of the lexer: the grammar's order elects.
  tree<ast::First>(Entry{"lineend:"}, "x q\na", [](const ast::First&) { return true; });
  // Before a `b`, a caret is offered `a` by the parse that reads it so,
  // though the other reads `ab` there.
  {
    const std::string source = Entry{"twotokens:"}.source("x q b");
    const auto at = static_cast<cursiv::TextOffset>(source.size() - 1);
    CHECK((caret_offers::offered(conformance_test::parser(), source, at) ==
           std::vector<std::string>{"a", "ab"}));
  }
  {
    const std::string source = many.source("xaxb");
    const auto at = static_cast<cursiv::TextOffset>(many.prefix.size() + 3u);
    CHECK((caret_offers::offered(conformance_test::parser(), source, at) ==
           std::vector<std::string>{"a", "ab"}));
  }

  // The caret reads a path as the parse does: the longer `TEXT` a merged
  // state flags is refused where the path's stack takes no `TEXT`, behind
  // the caret and in what is typed there.
  const Entry merged{"mergedlookahead:"};
  const std::string path = merged.source(" a.b");
  for (cursiv::TextOffset at = 17; at <= path.size(); ++at)
    CHECK(!caret_offers::offered(conformance_test::parser(), path, at).empty());
  CHECK((caret_offers::offered(conformance_test::parser(), path, 19) ==
         std::vector<std::string>{"ID"}));

  const Entry word{"wordhost:"};
  tree<ast::Leaf>(word, "[x]",
                  [](const ast::Leaf& n) { return n.text == "x"; });

  const Entry calls{"calls:"};
  tree<ast::Program>(calls, "a\n(b)", [](const ast::Program& n) {
    return n.statements.size() == 1 &&
           n.statements[0]->type == &cursiv::kAstTypeTag<ast::Call>;
  });
  tree<ast::Program>(calls, "a\nb", [](const ast::Program& n) {
    return n.statements.size() == 2 &&
           n.statements[0]->type == &cursiv::kAstTypeTag<ast::Name>;
  });
  const Entry memberCalls{"membercalls:"};
  for (const std::string_view text : {"f.g(x)", "f(x).g(y)", "(f).g(x).h(y)"})
    tree<ast::Program>(memberCalls, text, [](const ast::Program& n) {
      return n.statements.size() == 1 &&
             n.statements[0]->type == &cursiv::kAstTypeTag<ast::Call>;
    });

  // Empty nodes, regions crossing trivia and dropped tokens leave a tree whose
  // spans nest and stay in order.
  const Entry all{""};
  for (const std::string_view text :
       {"tie: if", "atendhost: [  a ]", "trailing: \n", "turn: ", "calls: ",
        "callermany: ", "keywordtrivia: bc   ", "keywordtrivia:shared\n",
        "%% trailing:", "calls: a) ", "calls: a a }", "calls: a a\nb",
        "conditionedtrivia: a) ", "conditionedtrivia: ) a",
        "conditionedtrivia: a; b c"})
    one_tree(all, text);

  // The lexer chooses the keyword at each occurrence of a statement, whatever
  // the statements before it read.
  const Entry closedHead{"closedhead:"};
  const std::string once = closedHead.source("b t module;");
  const std::string thrice =
      closedHead.source("b t module; b t module; b t module;");
  CHECK(conformance_test::parser().parse(once).diagnostics.size() == 1);
  CHECK(conformance_test::parser().parse(thrice).diagnostics.size() == 3);

  condition_contract_cases::run();
  template_tail_cases::run();
  section_predicate_cases::run();
  conformance_section_close_cases::run();
  branch_values_cases::run();
  return cursiv::test::report("Conformance_test");
}
