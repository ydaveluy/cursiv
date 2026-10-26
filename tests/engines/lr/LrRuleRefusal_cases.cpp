// Lookaheads on the token in hand, and past it as far as a completion reads.

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/lr/RuleRefusalGrammars.hpp"
#include "rulerefusal/generated/G/Module.hpp"

// A refusal in a rule holds at its position alone; elsewhere a keyword is a
// name wherever the state reads names alone.
namespace rule_refusal {

namespace {

// `name` or `name=alias` per item of a clean reading behind `head`;
// `<refused>` otherwise.
bool read_items(const cursiv::Parser& p, std::string_view head,
           std::string_view body, std::vector<std::string> want) {
  const std::string text = std::string(head) + std::string(body);
  const cursiv::ParseResult r = p.parse(text);
  std::vector<std::string> got;
  if (!r.accepted || !r.diagnostics.empty()) {
    got = {"<refused>"};
  } else {
    for (const trefuse::ast::Item* const it :
         r.value_as<trefuse::ast::Doc>()->items)
      got.emplace_back(it->alias.empty()
                           ? std::string(it->name)
                           : std::string(it->name) + "=" +
                                 std::string(it->alias));
  }
  if (got == want) return true;
  std::printf("  %s ->", text.c_str());
  for (const std::string& g : got) std::printf(" [%s]", g.c_str());
  std::printf("\n");
  return false;
}

// Whether the completion at `|` offers `literal`, `replaced` bytes replaced.
bool offers(const cursiv::Parser& p, std::string text, unsigned replaced,
            std::string_view literal) {
  const auto at = static_cast<cursiv::TextOffset>(text.find('|'));
  text.erase(at, 1);
  const cursiv::ParseResult r = p.parse(text);
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), at, at + replaced, out);
  for (const auto& f : out)
    if (p.getGrammar().terminalText(f.feature) == literal) return true;
  return false;
}

// Whether the completion at `|` offers the terminal named `name`.
bool names(const cursiv::Parser& p, std::string text, std::string_view name) {
  const auto at = static_cast<cursiv::TextOffset>(text.find('|'));
  text.erase(at, 1);
  const cursiv::ParseResult r = p.parse(text);
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), at, at, out);
  for (const auto& f : out)
    if (p.getGrammar().terminalName(f.feature) == name) return true;
  return false;
}

}  // namespace

int run() {
  cursiv::test::count_expectations();
  const auto p = trefuse::head::generated::G::create_parser();
  {
    const auto items = [&](std::string_view body, std::vector<std::string> want) {
      return read_items(*p, "alias ", body, std::move(want));
    };
    CHECK(items("t AS LEFT", {"t=LEFT"}));
    CHECK(items("t AS left", {"t=left"}));
    CHECK(items("t AS ok", {"t=ok"}));
    CHECK(items("t ok", {"t=ok"}));
    CHECK(items("t LEFT JOIN u", {"t"}));
    CHECK(items("t AS LEFT LEFT JOIN u", {"t=LEFT"}));
    CHECK(items("t AS AS", {"<refused>"}));
    CHECK(items("t LEFT", {"<refused>"}));
  }
  {
    const auto items = [&](std::string_view body, std::vector<std::string> want) {
      return read_items(*p, "soft ", body, std::move(want));
    };
    CHECK(items("int x = y;", {"x=y"}));
    CHECK(items("int x = iffy;", {"x=iffy"}));
    CHECK(items("int x = int;", {"x=int"}));
    CHECK(items("int if = y;", {"if=y"}));
    CHECK(items("int int = if;", {"int=if"}));
    CHECK(items("int = y;", {"<refused>"}));
  }
  {
    const auto items = [&](std::string_view body, std::vector<std::string> want) {
      return read_items(*p, "", body, std::move(want));
    };
    CHECK(items("go iffy", {"iffy"}));
    CHECK(items("go ifx", {"ifx"}));
    CHECK(items("go if", {"<refused>"}));
    CHECK(items("put abc", {"abc"}));
    CHECK(items("put {abc}", {"{abc}"}));
    CHECK(items("ask if x", {"x"}));
    CHECK(items("ask x", {"x"}));
    CHECK(items("ask if\xf0\x9d\x92\xb3", {"if\xf0\x9d\x92\xb3"}));
    CHECK(offers(*p, "go |", 0, "iffy"));
    CHECK(offers(*p, "ask |", 0, "if"));
    CHECK(!offers(*p, "ask |x", 0, "if"));
    CHECK(!offers(*p, "ask |\xc3\xa9", 0, "if"));
    CHECK(!offers(*p, "ask |\xf0\x9d\x92\xb3", 0, "if"));
    // A named terminal has no bytes yet, but its body settles a condition:
    // the terminal the condition names is refused, one that starts as it
    // does may still be a token it reads whole.
    CHECK(names(*p, "put |", "BRACED"));
    CHECK(names(*p, "put |", "ID"));
    CHECK(!names(*p, "say |", "STR"));
    CHECK(names(*p, "say |", "WORD"));
    CHECK(items("num 12", {"12"}));
    CHECK(items("num x.5", {"x.5"}));
    CHECK(items("num 12.5", {"12.5"}));
    CHECK(items("num 1.", {"1."}));
    CHECK(items("only x", {"x"}));
    CHECK(items("only 12x", {"12x"}));
    CHECK(items("only 12", {"<refused>"}));
    CHECK(items("only 12 x", {"<refused>"}));
    CHECK(items("only 12x y", {"12x"}));
    CHECK(items("say abc", {"abc"}));
    CHECK(items("say 'ab'", {"<refused>"}));
    CHECK(items("head 12", {"12"}));
    CHECK(items("head x.5", {"x.5"}));
    CHECK(items("head 12.5", {"12.5"}));
    CHECK(items("head 12.", {"<refused>"}));
    CHECK(items("neg -x", {"x"}));
    CHECK(items("neg -1", {"<refused>"}));
    CHECK(items("pair ab", {"<refused>"}));
    CHECK(items("both ifx", {"ifx"}));
    CHECK(items("both if\xc3\xa9", {"if\xc3\xa9"}));
    CHECK(items("both if", {"<refused>"}));
    CHECK(items("both zzz", {"zzz"}));
    CHECK(items("both zz", {"<refused>"}));
    CHECK(items("also if", {"if"}));
    CHECK(items("also zz", {"zz"}));
    CHECK(items("also zzz", {"<refused>"}));
    CHECK(items("also if\xc3\xa9", {"<refused>"}));
    CHECK(items("also ifx", {"<refused>"}));
    CHECK(items("dash x", {"x"}));
    CHECK(items("dash - x", {"<refused>"}));
    CHECK(items("ends x", {"x"}));
    CHECK(items("ends x; go y", {"<refused>"}));
    CHECK(items("more x", {"x"}));
    CHECK(items("next a b", {"a"}));
    CHECK(items("next a; go x", {"a", "x"}));
    CHECK(items("next a", {"a"}));
  }
  return 0;
}

}  // namespace rule_refusal

// A condition the prefix asks may read into the text a completion replaces:
// each candidate decides it on its own text, so none the text would take is
// lost and none it would refuse is offered.
namespace caret_look {

namespace {

// The case's opening keyword in the grammar that holds it.
constexpr std::string_view kHead = "caret ";

bool clean(const cursiv::Parser& parser, std::string_view body) {
  const std::string text = std::string(kHead) + std::string(body);
  const cursiv::ParseResult r = parser.parse(text);
  return r.accepted && r.diagnostics.empty();
}

// Whether the byte after `|`, replaced, may be `literal`.
bool offers(const cursiv::Parser& parser, std::string_view body,
            std::string_view literal) {
  std::string text = std::string(kHead) + std::string(body);
  const auto at = text.find('|');
  text.erase(at, 1);
  const cursiv::ParseResult r = parser.parse(text);
  std::vector<cursiv::NextFeature> features;
  parser.nextFeaturesAt(r.cst(), at, at + 1, features);
  for (const auto& feature : features)
    if (parser.getGrammar().terminalText(feature.feature) == literal)
      return true;
  return false;
}

}  // namespace

int run() {
  const auto p = trefuse::head::generated::G::create_parser();
  for (const std::string_view text : {"+abx", "+aby", "-abx"})
    CHECK(clean(*p, text));
  CHECK(!clean(*p, "-aby"));
  CHECK(offers(*p, "+ab|x", "x") && offers(*p, "+ab|x", "y"));
  CHECK(offers(*p, "+ab|y", "x") && offers(*p, "+ab|y", "y"));
  CHECK(offers(*p, "-ab|x", "x") && !offers(*p, "-ab|x", "y"));
  CHECK(clean(*p, "/abcx") && !clean(*p, "/abcy"));
  CHECK(offers(*p, "/abc|x", "x") && !offers(*p, "/abc|x", "y"));
  return 0;
}

}  // namespace caret_look

// A condition that reads a run to any length, asked of a word, reads no
// further than the word and what its guards look past: the replay decides it
// on the text wherever the caret is farther, and the readings it keeps merge.
namespace caret_run {

namespace {

bool names(const cursiv::Parser& p, const cursiv::ParseResult& r,
           cursiv::TextOffset at, std::string_view name) {
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), at, at, out);
  for (const auto& f : out)
    if (p.getGrammar().terminalName(f.feature) == name) return true;
  return false;
}

}  // namespace

int run() {
  const auto p = trefuse::head::generated::G::create_parser();
  std::string text = "lines a / b";
  for (unsigned i = 0; i < 2000; ++i) text += " ; a / b";
  const cursiv::ParseResult r = p->parse(text);
  CHECK(r.accepted && r.diagnostics.empty());
  const auto at = static_cast<cursiv::TextOffset>(text.size() - 1);
  const auto begin = std::chrono::steady_clock::now();
  CHECK(names(*p, r, at, "ID") && names(*p, r, at, "NUM"));
  CHECK(std::chrono::steady_clock::now() - begin < std::chrono::seconds(5));
  const cursiv::ParseResult numbered = p->parse("lines a / 12. b ; c");
  CHECK(numbered.accepted && numbered.diagnostics.empty());
  CHECK(names(*p, numbered, 10, "NUM") && names(*p, numbered, 10, "ID"));
  return 0;
}

}  // namespace caret_run

int run_lr_rule_refusal_cases() {
  int status = 0;
  status |= rule_refusal::run();
  status |= caret_look::run();
  status |= caret_run::run();
  return status;
}
