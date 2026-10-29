// The conformance cases of the grammar reference, on both engines.

#include <array>
#include <cstdio>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "conformancell/generated/Ll/Module.hpp"
#include "conformancelr/generated/Lr/Module.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
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

using conformance_test::clean;
using conformance_test::expect;
using conformance_test::kEngine;
using conformance_test::Pair;

template <class T, class Check>
void tree(const Pair& pair, std::string_view text, Check&& check,
          std::source_location at = std::source_location::current()) {
  for (unsigned i = 0; i < 2; ++i) {
    const std::string source = pair.source(text);
    const auto r = pair[i]->parse(source);
    const auto* node = exactly<T>(r);
    bool ok = clean(r) && node != nullptr && check(*node);
    if (!ok)
      std::printf("%s [%.*s]: wrong tree (row %u)\n", kEngine[i],
                  static_cast<int>(text.size()), text.data(), at.line());
    CHECK(ok);
  }
}

// The two texts a `Won` or a `Pick` holds, on both engines.
void won(const Pair& pair, std::string_view text, std::string_view token,
         std::string_view name,
         std::source_location at = std::source_location::current()) {
  tree<tconformance::ast::Won>(
      pair, text,
      [&](const auto& n) { return n.token == token && n.name == name; }, at);
}
void pick(const Pair& pair, std::string_view text, std::string_view literal,
          std::string_view keyword,
          std::source_location at = std::source_location::current()) {
  tree<tconformance::ast::Pick>(
      pair, text,
      [&](const auto& n) {
        return n.literal == literal && n.keyword == keyword;
      },
      at);
}

}  // namespace

int main() {
  namespace ast = tconformance::ast;
  cursiv::test::count_expectations();

  const Pair keyword{"keyword:"};
  expect(keyword, "if", true);
  expect(keyword, "ifx", false);

  // A keyword and a literal of one spelling tie: the arm written first wins.
  const Pair keywordTie{"keywordtie:"};
  pick(keywordTie, "if(", "if", "");
  const Pair keywordTieBoundary{"keywordtieboundary:"};
  pick(keywordTieBoundary, "if x", "if", "");
  pick(keywordTieBoundary, "ifx", "if", "");
  const Pair firstArm{"firstarm:"};
  tree<ast::First>(firstArm, "if then", [](const ast::First&) { return true; });
  const Pair elected{"elected:"};
  tree<ast::First>(elected, "1 if;", [](const ast::First&) { return true; });
  tree<ast::Second>(elected, "1 if:", [](const ast::Second&) { return true; });
  won(elected, "2 if", "if", "");
  won(elected, "2 iff", "", "iff");
  won(elected, "3 if(", "if", "");
  won(elected, "3 if (", "", "if");
  tree<ast::Tally>(elected, "4 if if", [](const ast::Tally& n) {
    return n.turns.size() == 2 && !n.tail;
  });
  // The election comes first: its refused conversion is reported and no other
  // reading is tried.
  for (unsigned i = 0; i < 2; ++i) {
    const std::string source = elected.source("5 if");
    const auto r = elected[i]->parse(source);
    const auto* node = exactly<ast::Kept>(r);
    const bool ok =
        node != nullptr && node->value == "kept" && r.diagnostics.size() == 1 &&
        r.diagnostics[0].kind == cursiv::DiagnosticKind::conversion_error;
    if (!ok) std::printf("%s [5 if]: wrong conversion\n", kEngine[i]);
    CHECK(ok);
  }

  const Pair literalsRank{"literalsrank:"};
  won(literalsRank, "if", "if", "");
  won(literalsRank, "while", "while", "");
  won(literalsRank, "iff", "", "iff");
  const Pair literalsRankRefuses{"literalsrankrefuses:"};
  expect(literalsRankRefuses, "if", false);
  expect(literalsRankRefuses, "if;", true);
  expect(literalsRankRefuses, "x", true);

  const Pair literalConditionRank{"literalconditionrank:"};
  won(literalConditionRank, "if;", "if", "");
  won(literalConditionRank, "if ;", "", "if");
  won(literalConditionRank, "x;", "", "x");

  const Pair mixedRank{"mixedrank:"};
  pick(mixedRank, "if(", "if", "");
  pick(mixedRank, "x(", "", "x");

  const Pair conditionedRank{"conditionedrank:"};
  pick(conditionedRank, "if;", "if", "");
  pick(conditionedRank, "if:", "if", "");
  const Pair conditionedSwapped{"conditionedrankswapped:"};
  pick(conditionedSwapped, "if;", "", "if");
  pick(conditionedSwapped, "if:", "", "if");

  const Pair patternsTie{"patternstie:"};
  won(patternsTie, "if", "if", "");
  const Pair patternsTieSwapped{"patternstieswapped:"};
  won(patternsTieSwapped, "if", "", "if");

  const Pair mixedRefused{"mixedrefused:"};
  won(mixedRefused, "if;", "if", "");
  won(mixedRefused, "x;", "", "x");

  const Pair mixedLeading{"mixedleading:"};
  won(mixedLeading, "if;", "if", "");
  won(mixedLeading, "x;", "", "x");

  const Pair keywordTrivia{"keywordtrivia:"};
  won(keywordTrivia, "if", "", "if");
  won(keywordTrivia, "iff", "", "iff");
  const Pair literalTrivia{"literaltrivia:"};
  won(literalTrivia, "if", "", "if");
  won(literalTrivia, "iff", "", "iff");
  const Pair conditionedTrivia{"conditionedtrivia:"};
  won(conditionedTrivia, "if;", "", "if");
  const Pair guardedLiteral{"guardedliteral:"};
  won(guardedLiteral, "if;", "if", "");
  won(guardedLiteral, "if(", "", "if");
  const Pair literalBesideGuard{"literalbesideguard:"};
  won(literalBesideGuard, "if", "if", "");
  won(literalBesideGuard, "iff", "", "iff");
  // The lexer tries what the readings take, not what the state merged from
  // its callers would: `TEXT` is no rival of `.` where a path is a key.
  const Pair mergedLookahead{"mergedlookahead:"};
  expect(mergedLookahead, "a.b", true);
  expect(mergedLookahead, "a.b: [@c.d e.f]", true);
  const Pair expectedKeyword{"expectedkeyword:"};
  won(expectedKeyword, "REM ; REM junk\nabc", "REM", "abc");

  const Pair arms{"arms:"};
  tree<ast::Arms>(arms, "abc:", [](const ast::Arms& n) {
    return n.name == "abc" && n.low.empty();
  });
  tree<ast::Arms>(arms, "abc;", [](const ast::Arms& n) {
    return n.low == "abc" && n.name.empty();
  });

  // A branch, or an optional item, that reads what an earlier one reads is
  // dropped: each case reads as the rule written without it.
  const Pair twins{"twins:"};
  const auto unchanged = [&](std::string_view twin, std::string_view text) {
    const std::string with =
        twins.source(std::string(twin) + " " + std::string(text));
    const std::string without =
        twins.source(std::string(twin) + ". " + std::string(text));
    for (unsigned i = 0; i < 2; ++i) {
      const auto a = twins[i]->parse(with);
      const auto b = twins[i]->parse(without);
      const auto* x = exactly<ast::Won>(a);
      const auto* y = exactly<ast::Won>(b);
      const bool ok = clean(a) && clean(b) && x != nullptr && y != nullptr &&
                      x->token == y->token && x->name == y->name;
      if (!ok)
        std::printf("%s [%s]: reads unlike the rule without its twin\n",
                    kEngine[i], with.c_str());
      CHECK(ok);
    }
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

  const Pair separated{"separated:"};
  tree<ast::List>(separated, "",
                  [](const ast::List& n) { return n.items.empty(); });
  tree<ast::List>(separated, "a , a",
                  [](const ast::List& n) { return n.items.size() == 2; });
  expect(separated, ",", false);
  expect(separated, "a ,", false);

  const Pair trailing{"trailing:"};
  tree<ast::List>(trailing, "a,",
                  [](const ast::List& n) { return n.items.size() == 1; });
  tree<ast::List>(trailing, "a , a ,",
                  [](const ast::List& n) { return n.items.size() == 2; });
  expect(trailing, ",", false);

  const Pair grouped{"grouped:"};
  tree<ast::S>(grouped, "x y z", [](const ast::S& n) {
    return n.a != nullptr && n.a->letters.size() == 2 && n.b == nullptr &&
           n.c != nullptr;
  });
  const Pair repeated{"repeated:"};
  tree<ast::S>(repeated, "x y z", [](const ast::S& n) {
    return n.a != nullptr && n.a->letters.size() == 1 && n.b != nullptr &&
           n.b->letters.size() == 2 && n.c == nullptr;
  });

  const Pair turn{"turn:"};
  tree<ast::Turns>(turn, "aa", [](const ast::Turns& n) {
    return n.turns.size() == 1 && n.turns[0]->letters.size() == 2;
  });
  tree<ast::Turns>(turn, "aaa", [](const ast::Turns& n) {
    return n.turns.size() == 2 && n.turns[0]->letters.size() == 2 &&
           n.turns[1]->letters.size() == 1;
  });

  const Pair turnShortFirst{"turnshortfirst:"};
  tree<ast::Turns>(turnShortFirst, "aa", [](const ast::Turns& n) {
    return n.turns.size() == 2 && n.turns[0]->letters.size() == 1 &&
           n.turns[1]->letters.size() == 1;
  });

  const Pair tight{"tight:"};
  expect(tight, "[name]", true);
  expect(tight, "[ name ]", false);

  const Pair adjacent{"adjacent:"};
  expect(adjacent, ">>", true);
  expect(adjacent, "> >", false);

  const Pair closers{"closers:"};
  expect(closers, "<a>x", true);
  expect(closers, "<a>y>x", false);

  const Pair longString{"longstring:"};
  expect(longString, "[[x]]", true);
  expect(longString, "[=[x]=]", true);
  expect(longString, "[=[x]==]", false);

  const Pair split{"split:"};
  expect(split, "ab", false);
  expect(split, "a b", true);
  expect(split, "ab c", true);

  const Pair many{"callermany:"};
  const Pair ladder{"ladder:"};
  for (const Pair* shared : {&many, &ladder}) {
    for (const std::string_view text :
         {"xab", "xaxab", "xa xab", "xaxa xab", "xaxaxab"})
      expect(*shared, text, false);
    expect(*shared, "xa xab y", true);
  }
  for (const auto& parser : many) {
    const std::string source = many.source("xaxb");
    const auto parsed = parser->parse(source);
    std::vector<cursiv::NextFeature> next;
    const auto at = static_cast<cursiv::TextOffset>(many.prefix.size() + 3u);
    parser->nextFeaturesAt(parsed.cst(), at, at, next);
    CHECK(next.size() == 1);
    for (const cursiv::NextFeature& f : next) {
      const auto spellings = parser->getGrammar().terminalSpellings(f.feature);
      CHECK(spellings.size() == 1 && spellings.front() == "ab");
    }
  }

  // The caret reads a path as the parse does: the longer `TEXT` a merged
  // state flags is refused where the path's stack takes no `TEXT`, behind
  // the caret and in what is typed there.
  const Pair merged{"mergedlookahead:"};
  const std::string path = merged.source(" a.b");
  for (cursiv::TextOffset at = 17; at <= path.size(); ++at) {
    const auto ll = caret_offers::offered(*merged[0], path, at);
    const auto lr = caret_offers::offered(*merged[1], path, at);
    CHECK(!lr.empty() && ll == lr);
  }
  CHECK((caret_offers::offered(*merged[1], path, 19) ==
         std::vector<std::string>{"ID"}));

  const Pair word{"wordhost:"};
  tree<ast::Leaf>(word, "[x]",
                  [](const ast::Leaf& n) { return n.text == "x"; });

  const Pair calls{"calls:"};
  tree<ast::Program>(calls, "a\n(b)", [](const ast::Program& n) {
    return n.statements.size() == 1 &&
           n.statements[0]->type == &cursiv::ast_type_tag<ast::Call>;
  });
  tree<ast::Program>(calls, "a\nb", [](const ast::Program& n) {
    return n.statements.size() == 2 &&
           n.statements[0]->type == &cursiv::ast_type_tag<ast::Name>;
  });

  // A token alone stands for the node over it, a node that read nothing
  // stands where the text before it stops, the trivia a region crosses to its
  // own end is that region's, a token dropped after a whole construct stands
  // outside it, and the root holds what a repair dropped before its first
  // token.
  const Pair all{""};
  for (const std::string_view text :
       {"tie: if", "atendhost: [  a ]", "trailing: \n", "turn: ", "calls: ",
        "callermany: ", "keywordtrivia: bc   ", "keywordtrivia:shared\n",
        "%% trailing:", "calls: a) ", "calls: a a }",
        "conditionedtrivia: a) ", "conditionedtrivia: ) a"})
    one_tree(all, text);

  condition_contract_cases::run();
  template_tail_cases::run();
  section_predicate_cases::run();
  conformance_section_close_cases::run();
  branch_values_cases::run();
  return cursiv::test::report("Conformance_test");
}
