#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/OpeningTokenGrammar.hpp"

#include "openingshiftll/generated/ShiftLl/Module.hpp"
#include "openingnegatedll/generated/NegatedLl/Module.hpp"
#include "openingtablesll/generated/TablesLl/Module.hpp"
#include "openingcompositell/generated/CompositeLl/Module.hpp"
#include "openingshiftlr/generated/ShiftLr/Module.hpp"

namespace {

using namespace topening;

std::string show(const ast::Expression* e) {
  if (const auto* n = cursiv::ast_cast<ast::Number>(e))
    return std::string(n->text);
  if (const auto* g = cursiv::ast_cast<ast::Negation>(e))
    return "(not " + show(g->operand) + ")";
  const auto* b = cursiv::ast_cast<ast::Binary>(e);
  if (b == nullptr) return "?";
  std::string op;
  for (const char c : b->op)
    if (c != ' ') op += c;
  return "(" + show(b->left) + " " + op + " " + show(b->right) + ")";
}

std::string read(const cursiv::Parser& parser, std::string_view text) {
  const auto r = parser.parse(text);
  if (!r.accepted || !r.diagnostics.empty()) return "refused";
  const auto* doc = cursiv::ast_cast<ast::Document>(r.value);
  return doc == nullptr ? "?" : show(doc->value);
}

struct Case {
  std::string_view text, tree;
};

struct Pair {
  const cursiv::Parser& ll;
  const cursiv::Parser* lr;
};

void expect_cases(Pair p, std::initializer_list<Case> cases) {
  for (const auto& [text, tree] : cases)
    for (const auto* parser : {&p.ll, p.lr}) {
      if (!parser) continue;
      const auto got = read(*parser, text);
      if (got != tree)
        std::printf("%.*s: got %s, want %.*s\n", int(text.size()),
                    text.data(), got.c_str(), int(tree.size()), tree.data());
      CHECK(got == tree);
    }
}

// What the grammar may spell at the end of `text`, each spelling once.
std::vector<std::string> spellings_after(const cursiv::Parser& parser,
                                         std::string_view text) {
  const auto r = parser.parse(text);
  std::vector<cursiv::NextFeature> next;
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  parser.nextFeaturesAt(r.cst(), at, at, next);
  std::vector<std::string> out;
  for (const auto& feature : next)
    out.emplace_back(parser.getGrammar().terminalText(feature.feature));
  std::ranges::sort(out);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

void same_spellings(Pair p, std::initializer_list<std::string_view> texts) {
  for (const std::string_view text : texts) {
    const auto a = spellings_after(p.ll, text), b = spellings_after(*p.lr, text);
    if (a != b) {
      std::printf("[%.*s] LL:", static_cast<int>(text.size()), text.data());
      for (const auto& x : a) std::printf(" '%s'", x.c_str());
      std::printf(" LR:");
      for (const auto& x : b) std::printf(" '%s'", x.c_str());
      std::printf("\n");
    }
    CHECK(a == b);
  }
}

// Expressions drawn at random from a language's operators, a few of them
// damaged: the engines read each alike, accepted or not.
void swarm(Pair p, std::initializer_list<std::string_view> ops) {
  const std::vector<std::string_view> spelled(ops);
  std::uint64_t seed = 777;
  const auto next = [&seed] {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<unsigned>(seed >> 33);
  };
  const auto op = [&] { return spelled[next() % spelled.size()]; };
  unsigned shown = 0;
  for (int i = 0; i < 1500; ++i) {
    std::string text;
    const unsigned operands = 1 + next() % 6;
    for (unsigned k = 0; k < operands; ++k) {
      if (k != 0) text += op();
      if (next() % 16 == 0 && k + 1 < operands)
        text += "(1" + std::string(op()) + "2)";
      else
        text += static_cast<char>('1' + next() % 3);
      if (next() % 12 == 0) text += ' ';
    }
    if (next() % 20 == 0) text.erase(next() % text.size(), 1);
    const std::string a = read(p.ll, text);
    const std::string b = read(*p.lr, text);
    if (a != b && shown++ < 8)
      std::printf("[%s] LL %s, LR %s\n", text.c_str(), a.c_str(), b.c_str());
    CHECK(a == b);
  }
}

}  // namespace

int main() {
  const auto shiftLl = topening::generated::ShiftLl::create_parser();
  const auto shiftLr = topening::generated::ShiftLr::create_parser();
  const auto negatedLl = topening::generated::NegatedLl::create_parser();
  const auto tablesLl = topening::generated::TablesLl::create_parser();
  const auto compositeLl = topening::generated::CompositeLl::create_parser();
  const Pair shift{*shiftLl, shiftLr.get()}, negated{*negatedLl, nullptr},
      tables{*tablesLl, nullptr}, composite{*compositeLl, nullptr};

  expect_cases(shift, {{"1>2", "(1 > 2)"},
                {"1>>2", "(1 >> 2)"},
                {"1 > > 2", "(1 >> 2)"},
                {"1<<2", "(1 << 2)"},
                {"1<2", "(1 < 2)"},
                {"1<2>>3", "(1 < (2 >> 3))"},
                {"1>>2>3", "((1 >> 2) > 3)"},
                {"1>2>>3", "(1 > (2 >> 3))"},
                {"1>>2>>3", "((1 >> 2) >> 3)"},
                {"1>2>3", "((1 > 2) > 3)"},
                {"1<2<<3", "(1 < (2 << 3))"},
                {"1<<2<3", "((1 << 2) < 3)"},
                {"1<<2>>3", "((1 << 2) >> 3)"},
                {"1+2>>3", "((1 + 2) >> 3)"},
                {"1>>2+3", "(1 >> (2 + 3))"},
                {"1*2>>3*4", "((1 * 2) >> (3 * 4))"},
                {"1>2>>3+4>5", "((1 > (2 >> (3 + 4))) > 5)"},
                {"(1>>2)>3", "((1 >> 2) > 3)"},
                {"1-2", "(1 - 2)"},
                {"1->2", "(1 -> 2)"},
                {"1-2->3", "((1 - 2) -> 3)"},
                {"1->2->3", "(1 -> (2 -> 3))"},
                {"1->2-3", "(1 -> (2 - 3))"},
                {"1>2->3", "((1 > 2) -> 3)"},
                {"1->2>3", "(1 -> (2 > 3))"},
                {"1->2>>3", "(1 -> (2 >> 3))"},
                {"1>>2->3->4", "((1 >> 2) -> (3 -> 4))"},
                {"1 - > 2", "(1 -> 2)"}});
  same_spellings(shift, {"1 ", "1-", "1>", "1<"});
  swarm(shift,
        {">", "<", ">>", "<<", "->", "-", "+", "*", "> >", "- >", "->", ">>"});

  expect_cases(negated,
        {{"1 in 2", "(1 in 2)"},
         {"1 like 2", "(1 like 2)"},
         {"1 not in 2", "(1 notin 2)"},
         {"1 not like 2", "(1 notlike 2)"},
         {"not 1", "(not 1)"},
         {"not 1 in 2", "((not 1) in 2)"},
         {"not 1 not in 2", "((not 1) notin 2)"},
         {"1 in not 2", "(1 in (not 2))"},
         {"1 not in not 2", "(1 notin (not 2))"},
         {"1 not in 2 like 3", "(1 notin (2 like 3))"},
         {"1 not like 2 in 3", "((1 notlike 2) in 3)"},
         {"1 like 2 not in 3", "((1 like 2) notin 3)"},
         {"1 in 2 not like 3", "(1 in (2 notlike 3))"},
         {"1 not like 2 not in 3", "((1 notlike 2) notin 3)"},
         {"1 not in 2 not like 3", "(1 notin (2 notlike 3))"},
         {"1 not in 2 not in 3", "((1 notin 2) notin 3)"},
         {"1 + 2 not like 3 + 4", "((1 + 2) notlike (3 + 4))"},
         {"1 and 2 not in 3", "(1 and (2 notin 3))"},
         {"1 not in 2 and 3 not like 4", "((1 notin 2) and (3 notlike 4))"},
         {"1 not\nin 2", "(1 notin 2)"}});

  expect_cases(tables, {{"1>2", "(1 > 2)"},
                 {"1>>2", "(1 >> 2)"},
                 {"1>2>>3", "((1 > 2) >> 3)"},
                 {"1>>2>3", "(1 >> (2 > 3))"},
                 {"1>>2>>3", "((1 >> 2) >> 3)"},
                 {"1>2>3", "((1 > 2) > 3)"},
                 {"1<2>>3", "((1 < 2) >> 3)"},
                 {"1>>2?3>>4", "((1 >> 2) ? (3 >> 4))"},
                 {"1?2>>3", "(1 ? (2 >> 3))"},
                 {"1>2?3>4", "((1 > 2) ? (3 > 4))"}});

  expect_cases(composite, {{"1+2", "(1 + 2)"},
                    {"1+\n2", "(1 + 2)"},
                    {"1\n+2", "refused"},
                    {"1-2", "(1 - 2)"},
                    {"1 a b 2", "(1 ab 2)"},
                    {"1 a 2", "refused"},
                    {"1*x2", "(1 *x 2)"},
                    {"1 * y 2", "(1 *y 2)"},
                    {"1*2", "(1 * 2)"},
                    {"1*2*y3", "(1 * (2 *y 3))"},
                    {"1*x2*3", "((1 *x 2) * 3)"},
                    {"1+2*x3", "(1 + (2 *x 3))"},
                    {"1-2 a b 3", "((1 - 2) ab 3)"},
                    {"1+2 a b 3", "((1 + 2) ab 3)"},
                    {"1 a b 2+3", "(1 ab (2 + 3))"}});

  return cursiv::test::report("LlOpeningToken_test");
}
