#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/engines/ll/runtime/ll_lookahead.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/KeywordTriviaAst.hpp"
#include "keywordtriviabarell/generated/BareLl/Module.hpp"
#include "keywordtriviabasicll/generated/BasicLl/Module.hpp"
#include "keywordtriviabasicll/generated/BasicLl/Table.hpp"
#include "keywordtriviacapturedll/generated/VariantsLl/Module.hpp"
#include "keywordtriviawidell/generated/WideLl/Module.hpp"

namespace {

using namespace tllkwtrivia;

struct Case {
  std::string_view text;
  // Each statement as `target.part.part=value`, a `/` for a quotient, and the
  // texts the CST keeps as hidden trivia behind a `#`, each followed by `|`.
  std::string want;
};

std::string value_of(const Value* v) {
  if (const auto* r = cursiv::ast_cast<Ref>(v)) return std::string(r->text);
  if (const auto* q = cursiv::ast_cast<Quotient>(v))
    return value_of(q->left) + "/" + value_of(q->right);
  return "?";
}

template <class Parser>
std::string read(const Parser& p, std::string_view text,
                 std::string_view prefix = "") {
  const std::string input = std::string(prefix) + std::string(text);
  const auto result = p->parse(input);
  CHECK(result.diagnostics.empty());
  std::string out;
  if (const auto* doc = result.template value_as<Doc>())
    for (const Assign* s : doc->statements) {
      out += std::string(s->target);
      for (const Part* part : s->parts) out += "." + std::string(part->text);
      out += "=" + value_of(s->value) + ";";
    }
  out += "#";
  for (cursiv::NodeId i = 0; i < result.cst().size(); ++i) {
    const auto& n = result.cst()[i];
    if (n.hidden)
      out += input.substr(n.begin, n.end - n.begin) + '|';
  }
  return out;
}

template <class Ll>
void expect_cases(const Ll& ll, std::initializer_list<Case> cases,
                  std::string_view prefix = "") {
  for (const Case& k : cases) {
    const auto a = read(ll, k.text, prefix);
    if (a != k.want)
      std::cerr << k.text << "\nwant: " << k.want << "\nLL: " << a
                << '\n';
    CHECK(a == k.want);
  }
}

template <class Parser>
std::set<std::string> offered(const Parser& p, std::string_view text) {
  const auto r = p->parse(text);
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  std::vector<cursiv::NextFeature> out;
  p->nextFeaturesAt(r.cst(), at, at, out);
  std::set<std::string> seen;
  for (const auto& f : out)
    for (const auto t : p->getGrammar().terminalSpellings(f.feature))
      seen.insert(std::string(t));
  return seen;
}

// An excluded keyword can still cut a comment's DFA path. Asking only for
// that trivia must retain the error alphabet, even if a smaller mode covers it.
void hinted_trivia_keeps_keyword_cuts() {
  constexpr auto& table = generated::BasicLl::kTable;
  cursiv::LlMask<table.termWords> wanted;
  for (std::uint16_t term = 1; term < table.nterms; ++term)
    if (table.term_name(term) == "REMARK") wanted.set(term);
  constexpr std::string_view text = "REM a note";
  std::vector<std::uint16_t> plain, hinted;
  const auto end =
      cursiv::ll_match_in<table>(text.data(), text.size(), 0, wanted, plain);
  const auto narrowed = cursiv::ll_match_in<table>(
      text.data(), text.size(), 0, wanted, hinted, nullptr, nullptr, nullptr,
      table.modeTrivia[table.errMode]);
  CHECK(end == 0 && plain.empty());
  CHECK(narrowed == end && hinted == plain);
}

// The words of the statements a parse holds.
template <class Parser>
std::string parts_of(const Parser& p, std::string_view text) {
  const auto result = p->parse(text);
  CHECK(result.diagnostics.empty());
  std::string out;
  if (const auto* doc = result.template value_as<Parts>())
    for (const Part* part : doc->parts) out += std::string(part->text) + ' ';
  return out;
}

std::string pick(std::mt19937& rng,
                 std::initializer_list<std::string_view> of) {
  return std::string(*(of.begin() + rng() % of.size()));
}

// Valid statements, some behind a comment, the keyword in any case among the
// parts of a name.
std::string random_document(std::mt19937& rng, bool slashes) {
  std::string out;
  for (unsigned n = 1 + rng() % 6; n != 0; --n) {
    out += pick(rng, {"", " ", "\n"});
    if (rng() % 4 == 0)
      out +=
          slashes
              ? pick(rng, {"REM note\n", "rem a (b) c\n", "// c\n", "/* c */ "})
              : pick(rng, {"REM note\n", "rem a (b) c\n"});
    out += pick(rng, {"x", "a", "REMARK"});
    for (unsigned k = rng() % 3; k != 0; --k)
      out += "." + pick(rng, {"y", "REM", "rem", "Rem", "REMARK"});
    out += pick(rng, {"=", " = ", "  =", "= "});
    out += pick(rng, {"a", "b", "REMARK"});
    out += ";";
  }
  return out;
}

// The grammars that spell one comment in different ways read every valid
// document alike, in both engines.
void comments_spelled_alike_read_alike(const cursiv::Parser& basic,
                                      const cursiv::Parser& variants) {
  std::mt19937 rng(20261020);
  for (unsigned n = 0; n < 64; ++n) {
    const std::string text = random_document(rng, true);
    const std::string want = read(&basic, text);
    for (const std::string_view prefix : {"recognized:", "captured:"}) {
      const std::string got = read(&variants, text, prefix);
      if (got != want)
        std::cerr << text << "\nwant: " << want << "\ngot:  " << got << '\n';
      CHECK(got == want);
    }
  }
}

// A no-break space is a blank like a space is, and the keyword is whole at it.
void no_break_spaces_are_blanks(const cursiv::Parser& ll) {
  std::mt19937 rng(7);
  const auto statements = [](const std::string& out) {
    return out.substr(0, out.find('#'));
  };
  for (unsigned n = 0; n < 64; ++n) {
    const std::string ascii = random_document(rng, false);
    std::string wide;
    for (const char c : ascii) wide += c == ' ' ? "\u00a0" : std::string(1, c);
    const std::string want = statements(read(&ll, ascii));
    CHECK(statements(read(&ll, wide)) == want);
  }
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  hinted_trivia_keeps_keyword_cuts();
  namespace g = tllkwtrivia::generated;
  const auto ll = g::BasicLl::create_parser();
  const auto variants = g::VariantsLl::create_parser();
  for (const auto* parser : {&ll}) expect_cases(*parser,
        {{"x.REM = a;", "x.REM=a;#"}, {"x.rem=a;", "x.rem=a;#"},
         {"x.y.Rem = a;", "x.y.Rem=a;#"}, {"x.REMARK = a;", "x.REMARK=a;#"},
         {"REM a note\nx.y = a;", "x.y=a;#REM a note|"},
         {"x.y = a; rem again\nz = b;", "x.y=a;z=b;#rem again|"},
         {"a = b //c\n / d;", "a=b/d;#//c|"},
         {"a = b /* c */ / d;", "a=b/d;#/* c */|"},
         {"x.REM = a / b; REM tail\n", "x.REM=a/b;#REM tail|"}});
  expect_cases(variants,
        {{"x.y = a; rem (a (b)) again\nz = b;",
          "x.y=a;z=b;#rem (a (b)) again|"}}, "recognized:");
  comments_spelled_alike_read_alike(*ll, *variants);
  const auto wideLl = g::WideLl::create_parser();
  expect_cases(wideLl,
        {{"x.REM\u00a0= a;", "x.REM=a;#"},
         {"REM\u00a0note\nx.y = a;", "x.y=a;#REM\u00a0note|"},
         {"x.y = a;\u00a0rem tail\nz = b;", "x.y=a;z=b;#rem tail|"}}, "");
  no_break_spaces_are_blanks(*wideLl);
  const auto bareLl = g::BareLl::create_parser();
  for (const auto* parser : {&bareLl}) {
    CHECK(parts_of(*parser, "x\nRem\ny\n") == "x Rem y ");
    CHECK(parts_of(*parser, "REM\nx\n") == "REM x ");
    CHECK(parts_of(*parser, "x\nREM hello\ny\n") == "x y ");
    CHECK(parts_of(*parser, "Remx\nResume\nRe\nReDim\n") ==
          "Remx Resume Re ReDim ");
  }
  for (const auto* parser : {&ll}) expect_cases(*parser,
        {{"x?REM = a;", "x.REM=a;#"},
         {"REM a note\nx?y = a;", "x.y=a;#REM a note|"},
         {"x?REM\tnote\ny = a;", "x.y=a;#REM\tnote|"}});
  for (const auto* parser : {&ll}) expect_cases(*parser,
        {{"x!REM = a;", "x.REM=a;#"},
         {"x!y = a; rem again\nz = b;", "x.y=a;z=b;#rem again|"}});
  for (const auto* parser : {&ll}) expect_cases(*parser,
        {{"x:REM = a;", "x.REM=a;#"},
         {"x:y = a; x:remx = b;", "x.y=a;x.remx=b;#"}});
  for (const auto* parser : {&ll}) {
    const auto missing = (*parser)->parse("x.REM = ;");
    CHECK(!missing.diagnostics.empty());
    const auto* doc = missing.template value_as<Doc>();
    CHECK(doc && !doc->statements.empty() &&
          !doc->statements[0]->parts.empty() &&
          doc->statements[0]->parts[0]->text == "REM");
    const auto after = (*parser)->parse("x. = a; y.REM = b;");
    CHECK(!after.diagnostics.empty());
    const auto* later = after.template value_as<Doc>();
    CHECK(later && !later->statements.empty() &&
          !later->statements.back()->parts.empty() &&
          later->statements.back()->parts.back()->text == "REM");
    CHECK(offered(*parser, "x.").contains("REM"));
    CHECK(!offered(*parser, "").contains("REM"));
  }
  return cursiv::test::report("LlKeywordTrivia_test");
}
