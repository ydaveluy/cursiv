#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "branchvaluesll/generated/Ll/Module.hpp"
#include "branchvalueslr/generated/Lr/Module.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/BranchValuesGrammar.hpp"

namespace branch_values_cases {

// One boolean three ways -- a terminal whose alternatives each give a
// constant, the data rule giving the same constants, and the words in place:
// the same tree, the same records but for the rule they name, and the same
// words expected at the same carets.

namespace {

using namespace tbranch;

constexpr const char* kEngine[] = {"LL", "LR"};

const auto& engines() {
  static const std::array<std::unique_ptr<cursiv::Parser>, 2> parsers{
      tbranch::generated::Ll::create_parser(),
      tbranch::generated::Lr::create_parser()};
  return parsers;
}

bool clean(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty() &&
         r.error == cursiv::AstBuildError::none && r.value != nullptr;
}

// Each item as `name=value`, the value being the flag, the size or the name.
std::string items(const cursiv::ParseResult& r) {
  const auto* in = r.value_as<ast::Items>();
  if (!clean(r) || in == nullptr) return "refused";
  std::string out;
  for (const ast::Item* i : in->items) {
    out += std::string(i->name) + '=';
    if (!i->other.empty())
      out += i->other;
    else if (i->size != ast::Size::none)
      out += i->size == ast::Size::small ? "small" : "large";
    else
      out += i->flag ? "true" : "false";
    out += ' ';
  }
  return out;
}

// Each item as `name=value`, `show` giving the value, whether or not the
// parse repaired the text: `clean ` leads a document read as written.
template <class Show>
void built(std::string_view text, std::string_view want, Show show) {
  for (unsigned e = 0; e < 2; ++e) {
    const std::string source(text);
    const cursiv::ParseResult r = engines()[e]->parse(source);
    const auto* in = r.value_as<ast::Items>();
    std::string got = r.diagnostics.empty() ? "clean " : "";
    if (in != nullptr)
      for (const ast::Item* i : in->items)
        got += std::string(i->name) + '=' + show(*i) + ' ';
    if (got != want)
      std::printf("%s [%s]: got [%s], wanted [%s]\n", kEngine[e],
                  source.c_str(), got.c_str(), std::string(want).c_str());
    CHECK(got == want);
  }
}
std::string flag_of(const ast::Item& i) { return i.flag ? "true" : "false"; }
std::string count_of(const ast::Item& i) { return std::to_string(i.count); }

void values(std::string_view text, std::string_view want) {
  for (unsigned e = 0; e < 2; ++e) {
    const std::string source(text);
    const std::string got = items(engines()[e]->parse(source));
    if (got != want)
      std::printf("%s [%s]: got [%s], wanted [%s]\n", kEngine[e],
                  source.c_str(), got.c_str(), std::string(want).c_str());
    CHECK(got == want);
  }
}

// What may replace the word at the caret, as a completion asks: spellings,
// and the names of terminals that have none, each with the member it fills.
std::vector<std::string> features(const cursiv::Parser& parser,
                                  std::string_view prefix,
                                  std::string_view body, std::size_t caret) {
  const auto word = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
  };
  std::size_t from = caret, to = caret;
  while (from > 0 && word(body[from - 1])) --from;
  while (to < body.size() && word(body[to])) ++to;
  const std::string text = std::string(prefix) + std::string(body);
  const cursiv::ParseResult parsed = parser.parse(text);
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(
      parsed.cst(), static_cast<cursiv::TextOffset>(prefix.size() + from),
      static_cast<cursiv::TextOffset>(prefix.size() + to), next);
  std::vector<std::string> out;
  const auto& g = parser.getGrammar();
  for (const cursiv::NextFeature& f : next) {
    const std::string member = ':' + std::string(f.property);
    const auto spellings = g.terminalSpellings(f.feature);
    for (const std::string_view spelled : spellings)
      out.push_back(std::string(spelled) + member);
    if (spellings.empty())
      out.push_back(std::string(g.terminalName(f.feature)) + member);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string show(const std::vector<std::string>& entries) {
  std::string out;
  for (const std::string& e : entries) out += ' ' + e;
  return out;
}

// At every caret of a document and of every cut of it, through the terminal,
// the data rule and in place.
void same_carets(std::string_view document) {
  for (unsigned e = 0; e < 2; ++e) {
    const cursiv::Parser& parser = *engines()[e];
    std::size_t asked = 0, alike = 0;
    for (std::size_t cut = 0; cut <= document.size(); ++cut) {
      const std::string_view body = document.substr(0, cut);
      for (std::size_t at = 0; at <= body.size(); ++at) {
        const auto a = features(parser, "token:", body, at);
        const auto b = features(parser, "rule:", body, at);
        const auto c = features(parser, "inline:", body, at);
        ++asked;
        alike += a == b && a == c;
        if (a != b || a != c)
          std::printf("%s [%.*s] at %zu\n  token: %s\n  rule:  %s\n"
                      "  inline:%s\n",
                      kEngine[e], static_cast<int>(body.size()), body.data(),
                      at, show(a).c_str(), show(b).c_str(), show(c).c_str());
      }
    }
    CHECK(alike == asked);
  }
}

// What an editor reads of each record that names a rule or a member: its
// text, the rule and the member. The words `as` reads keep the record of the
// rule around them.
void records(std::string_view text, std::string_view want) {
  for (unsigned e = 0; e < 2; ++e) {
    const std::string source(text);
    const cursiv::ParseResult r = engines()[e]->parse(source);
    const cursiv::Cst& cst = r.cst();
    const auto& g = engines()[e]->getGrammar();
    std::string got;
    for (cursiv::NodeId id = 0; id < cst.size(); ++id) {
      const cursiv::CstNode& n = cst[id];
      const std::string_view rule = g.recordRule(n);
      const std::string_view member = g.recordFeature(n);
      if (n.hidden || (rule.empty() && member.empty())) continue;
      got += '[' + std::string(cst.text(id)) + '|' + std::string(rule) + '|' +
             std::string(member) + ']';
    }
    if (got != want)
      std::printf("%s [%s]: got [%s]\n", kEngine[e], source.c_str(),
                  got.c_str());
    CHECK(got == want);
  }
}

}  // namespace

void run() {
  // Any case of the words, through the terminal, the data rule and in place;
  // a longer word is a name.
  for (const char* form : {"token:", "rule:", "inline:"})
    values(std::string(form) +
               " a = TRUE; b = false; c = True; d = fAlSe; e = Truex;",
           "a=true b=false c=true d=false e=Truex ");

  // `s` is spelled by both alternatives and the first one wins; `S` only by
  // the second. A word no alternative spells is no token.
  values("size: a : s; b : S; c : small; d : large; e : LARGE;",
         "a=small b=large c=small d=large e=large ");
  values("size: a : Small;", "refused");
  values("size: a : medium;", "refused");

  // The same carets, where a boolean can stand and elsewhere.
  for (unsigned e = 0; e < 2; ++e) {
    for (const char* form : {"token:", "rule:", "inline:"})
      CHECK(features(*engines()[e], form, "a = ", 4) ==
            std::vector<std::string>{"FALSE:flag", "ID:other", "TRUE:flag"});
    CHECK(features(*engines()[e], "flag:", "a ~ ", 4) ==
          std::vector<std::string>{"FALSE:flag", "TRUE:flag"});
    CHECK(features(*engines()[e], "flag:", "a ! ", 4) ==
          std::vector<std::string>{"FALSE:flag", "TRUE:flag"});
    CHECK(features(*engines()[e], "flag:", "a ? ", 4) ==
          std::vector<std::string>{"NO:flag", "YES:flag"});
  }
  same_carets("a = TRUE; b = false; c = d;");

  // A misspelled word gives the constant of the word it is read as, in every
  // form.
  built("flag: a ! TRUR; b ! fAlSr; c ~ TRUR; d ~ fAlSr; e * TRUR; f * fAlSr; "
        "g ? YAS;",
        "a=true b=false c=true d=false e=true f=false g=true ", flag_of);

  // One record a word, naming the rule written there, if any.
  records("token: a = TRUE;", "[a|ID|name][TRUE|BOOL|flag][a = TRUE;||items]");
  records("rule: a = TRUE;", "[a|ID|name][TRUE|Bool|flag][a = TRUE;||items]");
  records("inline: a = TRUE;", "[a|ID|name][TRUE||flag][a = TRUE;||items]");
  records("flag: a ! TRUE; c ? no;",
          "[a|ID|name][TRUE|Bool|flag][a ! TRUE;||items][c|ID|name][no||flag]"
          "[c ? no;||items]");
  records("count: c % ( abc ); d # x y;",
          "[c|ID|name][abc|ID|][( abc )|Measure|count][c % ( abc );||items]"
          "[d|ID|name][x|ID|][y|ID|][x y||count][d # x y;||items]");

  // `as` converts the text or the parts of what it reads, a data rule's
  // alternative as a member's payload.
  built("count: a # x y z; b # 42; c % ( abc ); d % [ p q ]; e % [ ];",
        "clean a=3 b=42 c=5 d=4 e=2 ", count_of);
  built("count: a # ; b # 4x2; c % ( abc ; d % [ p q ;", "a=0 b=4 c=0 d=0 ",
        count_of);
  built("count: c % ( abc ] ; d % [ p q ) ; e # x 1 y ; f % ( g h ) ;",
        "c=0 d=0 e=2 f=3 ", count_of);
}

}  // namespace branch_values_cases
