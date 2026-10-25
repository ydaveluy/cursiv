// The conformance cases of the grammar reference, on both engines.

#include <array>
#include <cstdio>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/ConformanceGrammars.hpp"
#include "conformancell/generated/Ll/Module.hpp"
#include "conformancelr/generated/Lr/Module.hpp"
namespace {

struct Pair {
  std::array<std::unique_ptr<cursiv::Parser>, 2> engines;
  std::string_view prefix;
  Pair(std::unique_ptr<cursiv::Parser> ll, std::unique_ptr<cursiv::Parser> lr,
       std::string_view tag = {}) : engines{std::move(ll), std::move(lr)}, prefix(tag) {}
  const auto& operator[](std::size_t i) const { return engines[i]; }
  auto begin() const { return engines.begin(); }
  auto end() const { return engines.end(); }
  std::string source(std::string_view text) const {
    return std::string(prefix) + std::string(text);
  }
};
constexpr const char* kEngine[] = {"LL", "LR"};

bool clean(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty() &&
         r.error == cursiv::AstBuildError::none;
}

void expect(const Pair& pair, std::string_view text, bool want) {
  for (unsigned i = 0; i < 2; ++i) {
    const std::string source = pair.source(text);
    const bool got = clean(pair[i]->parse(source));
    if (got != want)
      std::printf("%s [%.*s]: wanted %s\n", kEngine[i],
                  static_cast<int>(text.size()), text.data(),
                  want ? "read" : "refused");
    CHECK(got == want);
  }
}

template <class T>
const T* exactly(const cursiv::ParseResult& r) {
  return r.value != nullptr && r.value->type == &cursiv::ast_type_tag<T>
             ? static_cast<const T*>(r.value)
             : nullptr;
}

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

}  // namespace

int main() {
  namespace g = tconformance::generated;
  namespace ast = tconformance::ast;
  cursiv::test::count_expectations();

  const Pair keyword{g::Ll::create_parser(), g::Lr::create_parser(), "keyword:"};
  expect(keyword, "if", true);
  expect(keyword, "ifx", false);

  // A keyword and a literal of one spelling tie: the arm written first wins.
  const Pair keywordTie{g::Ll::create_parser(), g::Lr::create_parser(), "keywordtie:"};
  tree<ast::Pick>(keywordTie, "if(", [](const ast::Pick& n) {
    return n.literal == "if" && n.keyword.empty();
  });
  const Pair keywordTieBoundary{g::Ll::create_parser(), g::Lr::create_parser(), "keywordtieboundary:"};
  tree<ast::Pick>(keywordTieBoundary, "if x", [](const ast::Pick& n) {
    return n.literal == "if" && n.keyword.empty();
  });
  tree<ast::Pick>(keywordTieBoundary, "ifx", [](const ast::Pick& n) {
    return n.literal == "if" && n.keyword.empty();
  });
  const Pair firstArm{g::Ll::create_parser(), g::Lr::create_parser(), "firstarm:"};
  tree<ast::First>(firstArm, "if then", [](const ast::First&) { return true; });
  const Pair elected{g::Ll::create_parser(), g::Lr::create_parser(), "elected:"};
  const auto won = [&](std::string_view text, std::string_view token,
                       std::string_view name) {
    tree<ast::Won>(elected, text, [&](const ast::Won& n) {
      return n.token == token && n.name == name;
    });
  };
  tree<ast::First>(elected, "1 if;", [](const ast::First&) { return true; });
  tree<ast::Second>(elected, "1 if:", [](const ast::Second&) { return true; });
  won("2 if", "if", "");
  won("2 iff", "", "iff");
  won("3 if(", "if", "");
  won("3 if (", "", "if");
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

  const Pair literalsRank{g::Ll::create_parser(), g::Lr::create_parser(), "literalsrank:"};
  tree<ast::Won>(literalsRank, "if", [](const ast::Won& n) {
    return n.token == "if" && n.name.empty();
  });
  tree<ast::Won>(literalsRank, "while", [](const ast::Won& n) {
    return n.token == "while" && n.name.empty();
  });
  tree<ast::Won>(literalsRank, "iff", [](const ast::Won& n) {
    return n.name == "iff" && n.token.empty();
  });
  const Pair literalsRankRefuses{g::Ll::create_parser(), g::Lr::create_parser(), "literalsrankrefuses:"};
  expect(literalsRankRefuses, "if", false);
  expect(literalsRankRefuses, "if;", true);
  expect(literalsRankRefuses, "x", true);

  const Pair literalConditionRank{g::Ll::create_parser(), g::Lr::create_parser(), "literalconditionrank:"};
  tree<ast::Won>(literalConditionRank, "if;", [](const ast::Won& n) {
    return n.token == "if" && n.name.empty();
  });
  tree<ast::Won>(literalConditionRank, "if ;", [](const ast::Won& n) {
    return n.name == "if" && n.token.empty();
  });
  tree<ast::Won>(literalConditionRank, "x;", [](const ast::Won& n) {
    return n.name == "x" && n.token.empty();
  });

  const Pair mixedRank{g::Ll::create_parser(), g::Lr::create_parser(), "mixedrank:"};
  tree<ast::Pick>(mixedRank, "if(", [](const ast::Pick& n) {
    return n.literal == "if" && n.keyword.empty();
  });
  tree<ast::Pick>(mixedRank, "x(", [](const ast::Pick& n) {
    return n.keyword == "x" && n.literal.empty();
  });

  const Pair conditionedRank{g::Ll::create_parser(), g::Lr::create_parser(), "conditionedrank:"};
  tree<ast::Pick>(conditionedRank, "if;", [](const ast::Pick& n) {
    return n.literal == "if" && n.keyword.empty();
  });
  tree<ast::Pick>(conditionedRank, "if:", [](const ast::Pick& n) {
    return n.literal == "if" && n.keyword.empty();
  });
  const Pair conditionedSwapped{g::Ll::create_parser(), g::Lr::create_parser(), "conditionedrankswapped:"};
  tree<ast::Pick>(conditionedSwapped, "if;", [](const ast::Pick& n) {
    return n.keyword == "if" && n.literal.empty();
  });
  tree<ast::Pick>(conditionedSwapped, "if:", [](const ast::Pick& n) {
    return n.keyword == "if" && n.literal.empty();
  });

  const Pair patternsTie{g::Ll::create_parser(), g::Lr::create_parser(), "patternstie:"};
  tree<ast::Won>(patternsTie, "if", [](const ast::Won& n) {
    return n.token == "if" && n.name.empty();
  });
  const Pair patternsTieSwapped{g::Ll::create_parser(), g::Lr::create_parser(), "patternstieswapped:"};
  tree<ast::Won>(patternsTieSwapped, "if", [](const ast::Won& n) {
    return n.name == "if" && n.token.empty();
  });

  const Pair mixedRefused{g::Ll::create_parser(), g::Lr::create_parser(), "mixedrefused:"};
  tree<ast::Won>(mixedRefused, "if;", [](const ast::Won& n) {
    return n.token == "if" && n.name.empty();
  });
  tree<ast::Won>(mixedRefused, "x;", [](const ast::Won& n) {
    return n.name == "x" && n.token.empty();
  });

  const Pair mixedLeading{g::Ll::create_parser(), g::Lr::create_parser(), "mixedleading:"};
  tree<ast::Won>(mixedLeading, "if;", [](const ast::Won& n) {
    return n.token == "if" && n.name.empty();
  });
  tree<ast::Won>(mixedLeading, "x;", [](const ast::Won& n) {
    return n.name == "x" && n.token.empty();
  });

  const Pair keywordTrivia{g::Ll::create_parser(), g::Lr::create_parser(), "keywordtrivia:"};
  tree<ast::Won>(keywordTrivia, "if", [](const ast::Won& n) {
    return n.name == "if" && n.token.empty();
  });
  tree<ast::Won>(keywordTrivia, "iff", [](const ast::Won& n) {
    return n.name == "iff" && n.token.empty();
  });
  const Pair literalTrivia{g::Ll::create_parser(), g::Lr::create_parser(), "literaltrivia:"};
  tree<ast::Won>(literalTrivia, "if", [](const ast::Won& n) {
    return n.name == "if" && n.token.empty();
  });
  tree<ast::Won>(literalTrivia, "iff", [](const ast::Won& n) {
    return n.name == "iff" && n.token.empty();
  });
  const Pair conditionedTrivia{g::Ll::create_parser(), g::Lr::create_parser(), "conditionedtrivia:"};
  tree<ast::Won>(conditionedTrivia, "if;", [](const ast::Won& n) {
    return n.name == "if" && n.token.empty();
  });
  const Pair guardedLiteral{g::Ll::create_parser(), g::Lr::create_parser(), "guardedliteral:"};
  tree<ast::Won>(guardedLiteral, "if;", [](const ast::Won& n) {
    return n.token == "if" && n.name.empty();
  });
  tree<ast::Won>(guardedLiteral, "if(", [](const ast::Won& n) {
    return n.name == "if" && n.token.empty();
  });
  const Pair literalBesideGuard{g::Ll::create_parser(), g::Lr::create_parser(), "literalbesideguard:"};
  tree<ast::Won>(literalBesideGuard, "if", [](const ast::Won& n) {
    return n.token == "if" && n.name.empty();
  });
  tree<ast::Won>(literalBesideGuard, "iff", [](const ast::Won& n) {
    return n.name == "iff" && n.token.empty();
  });
  // The lexer tries what the readings take, not what the state merged from
  // its callers would: `TEXT` is no rival of `.` where a path is a key.
  const Pair mergedLookahead{g::Ll::create_parser(), g::Lr::create_parser(), "mergedlookahead:"};
  expect(mergedLookahead, "a.b", true);
  expect(mergedLookahead, "a.b: [@c.d e.f]", true);
  const Pair expectedKeyword{g::Ll::create_parser(), g::Lr::create_parser(), "expectedkeyword:"};
  tree<ast::Won>(expectedKeyword, "REM ; REM junk\nabc", [](const ast::Won& n) {
    return n.token == "REM" && n.name == "abc";
  });

  const Pair arms{g::Ll::create_parser(), g::Lr::create_parser(), "arms:"};
  tree<ast::Arms>(arms, "abc:", [](const ast::Arms& n) {
    return n.name == "abc" && n.low.empty();
  });
  tree<ast::Arms>(arms, "abc;", [](const ast::Arms& n) {
    return n.low == "abc" && n.name.empty();
  });

  const Pair separated{g::Ll::create_parser(), g::Lr::create_parser(), "separated:"};
  tree<ast::List>(separated, "",
                  [](const ast::List& n) { return n.items.empty(); });
  tree<ast::List>(separated, "a , a",
                  [](const ast::List& n) { return n.items.size() == 2; });
  expect(separated, ",", false);
  expect(separated, "a ,", false);

  const Pair trailing{g::Ll::create_parser(), g::Lr::create_parser(), "trailing:"};
  tree<ast::List>(trailing, "a,",
                  [](const ast::List& n) { return n.items.size() == 1; });
  tree<ast::List>(trailing, "a , a ,",
                  [](const ast::List& n) { return n.items.size() == 2; });
  expect(trailing, ",", false);

  const Pair grouped{g::Ll::create_parser(), g::Lr::create_parser(), "grouped:"};
  tree<ast::S>(grouped, "x y z", [](const ast::S& n) {
    return n.a != nullptr && n.a->letters.size() == 2 && n.b == nullptr &&
           n.c != nullptr;
  });
  const Pair repeated{g::Ll::create_parser(), g::Lr::create_parser(), "repeated:"};
  tree<ast::S>(repeated, "x y z", [](const ast::S& n) {
    return n.a != nullptr && n.a->letters.size() == 1 && n.b != nullptr &&
           n.b->letters.size() == 2 && n.c == nullptr;
  });

  const Pair turn{g::Ll::create_parser(), g::Lr::create_parser(), "turn:"};
  tree<ast::Turns>(turn, "aa", [](const ast::Turns& n) {
    return n.turns.size() == 1 && n.turns[0]->letters.size() == 2;
  });
  tree<ast::Turns>(turn, "aaa", [](const ast::Turns& n) {
    return n.turns.size() == 2 && n.turns[0]->letters.size() == 2 &&
           n.turns[1]->letters.size() == 1;
  });

  const Pair turnShortFirst{g::Ll::create_parser(), g::Lr::create_parser(), "turnshortfirst:"};
  tree<ast::Turns>(turnShortFirst, "aa", [](const ast::Turns& n) {
    return n.turns.size() == 2 && n.turns[0]->letters.size() == 1 &&
           n.turns[1]->letters.size() == 1;
  });

  const Pair tight{g::Ll::create_parser(), g::Lr::create_parser(), "tight:"};
  expect(tight, "[name]", true);
  expect(tight, "[ name ]", false);

  const Pair adjacent{g::Ll::create_parser(), g::Lr::create_parser(), "adjacent:"};
  expect(adjacent, ">>", true);
  expect(adjacent, "> >", false);

  const Pair closers{g::Ll::create_parser(), g::Lr::create_parser(), "closers:"};
  expect(closers, "<a>x", true);
  expect(closers, "<a>y>x", false);

  const Pair longString{g::Ll::create_parser(), g::Lr::create_parser(), "longstring:"};
  expect(longString, "[[x]]", true);
  expect(longString, "[=[x]=]", true);
  expect(longString, "[=[x]==]", false);

  const Pair split{g::Ll::create_parser(), g::Lr::create_parser(), "split:"};
  expect(split, "ab", false);
  expect(split, "a b", true);
  expect(split, "ab c", true);

  const Pair many{g::Ll::create_parser(), g::Lr::create_parser(), "callermany:"};
  const Pair ladder{g::Ll::create_parser(), g::Lr::create_parser(), "ladder:"};
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
    CHECK(next.size() == 1 &&
          parser->getGrammar().terminalText(next[0].feature) == "ab");
  }

  const Pair atEnd{g::Ll::create_parser(), g::Lr::create_parser(), "atendhost:"};
  expect(atEnd, "[a]", true);

  const Pair word{g::Ll::create_parser(), g::Lr::create_parser(), "wordhost:"};
  tree<ast::Leaf>(word, "[x]",
                  [](const ast::Leaf& n) { return n.text == "x"; });

  const Pair calls{g::Ll::create_parser(), g::Lr::create_parser(), "calls:"};
  tree<ast::Program>(calls, "a\n(b)", [](const ast::Program& n) {
    return n.statements.size() == 1 &&
           n.statements[0]->type == &cursiv::ast_type_tag<ast::Call>;
  });
  tree<ast::Program>(calls, "a\nb", [](const ast::Program& n) {
    return n.statements.size() == 2 &&
           n.statements[0]->type == &cursiv::ast_type_tag<ast::Name>;
  });

  return cursiv::test::report("Conformance_test");
}
