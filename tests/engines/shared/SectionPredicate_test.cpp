#include "sectionpredicatell/generated/Ll/Module.hpp"
#include "sectionpredicatelr/generated/Lr/Module.hpp"
#include "conformancell/generated/Ll/Module.hpp"
#include "conformancelr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/ConformanceGrammars.hpp"
#include "engines/shared/SectionPredicateGrammar.hpp"
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace section_predicate_cases {

// A predicate in an embedded grammar reads what the grammar alone reads: its
// text ends where the section does, whichever engine parses it.

namespace {

using namespace tsecpred;

std::string items(const ast::Items* in) {
  if (in == nullptr) return "-";
  std::string out;
  for (const ast::Item* i : in->items)
    out += std::string(i->text) +
           (i->type == &cursiv::ast_type_tag<ast::Marked> ? "!" : "") + ',';
  return out;
}

std::string diagnostics(const cursiv::ParseResult& r, int shift) {
  std::string out;
  for (const auto& d : r.diagnostics)
    out += ' ' + std::to_string(static_cast<int>(d.kind)) + '@' +
           std::to_string(static_cast<int>(d.begin) - shift) + '-' +
           std::to_string(static_cast<int>(d.end) - shift);
  return out;
}

struct Reader {
  std::unique_ptr<cursiv::Parser> parser;
  std::string_view prefix;
};

std::string alone(const Reader& p, std::string_view text) {
  const std::string source = std::string(p.prefix) + std::string(text);
  const auto r = p.parser->parse(source);
  return items(r.value_as<ast::Items>()) + diagnostics(r, p.prefix.size());
}

std::string embedded(const Reader& p, std::string_view text) {
  const std::string host = std::string(p.prefix) + '<' + std::string(text) + '>';
  const auto r = p.parser->parse(host);
  const auto* d = r.value_as<ast::Doc>();
  const std::string tree =
      d == nullptr || d->blocks.size() != 1 ? "-" : items((*d->blocks.begin())->items);
  return tree + diagnostics(r, p.prefix.size() + 1);
}

void same(const Reader& one, const Reader& host,
          std::string_view text) {
  const auto a = alone(one, text);
  const auto b = embedded(host, text);
  if (a != b)
    std::printf("\"%.*s\": alone %s, embedded %s\n", static_cast<int>(text.size()),
                text.data(), a.c_str(), b.c_str());
  CHECK(a == b);
}

}  // namespace

void run() {
  namespace gen = tsecpred::generated;
  const Reader lastLl{gen::Ll::create_parser(), ""};
  const Reader lastLr{gen::Lr::create_parser(), ""};
  const Reader inLastLl{gen::Ll::create_parser(), "inlast:"};
  const Reader inLastLr{gen::Lr::create_parser(), "inlast:"};
  const Reader reachLl{gen::Ll::create_parser(), "reach:"};
  const Reader reachLr{gen::Lr::create_parser(), "reach:"};
  const Reader inReachLl{gen::Ll::create_parser(), "inreach:"};
  const Reader inReachLr{gen::Lr::create_parser(), "inreach:"};
  for (const std::string_view text : {"a", "a b", "a b c", "ab cd", " a b "}) {
    same(lastLl, inLastLl, text);
    same(lastLr, inLastLr, text);
    CHECK(alone(lastLl, text) == alone(lastLr, text));
    same(reachLl, inReachLl, text);
    same(reachLr, inReachLr, text);
    CHECK(alone(reachLl, text) == alone(reachLr, text));
  }
  CHECK(alone(lastLl, "a b") == "a,b!,");
  CHECK(alone(reachLl, "a b") == "a,b,");
  // A delimiter inside a longer token does not end the section, even when
  // the guard's own reading stops before that token does.
  for (const std::string_view text : {"a > b !", "a > b > c !"}) {
    same(lastLl, inLastLl, text);
    same(lastLr, inLastLr, text);
    CHECK(alone(lastLl, text).empty());
    CHECK(alone(lastLr, text).empty());
  }
  // Every short guard shares an alphabet with LONG. A missing `!` must not
  // make section bounding read the remaining long suffix at every word.
  std::string words;
  for (unsigned i = 0; i != 8192; ++i) words += "a ";
  words += 'a';
  same(lastLl, inLastLl, words);
  same(lastLr, inLastLr, words);
  CHECK(alone(lastLl, words) == alone(lastLr, words));

}

}  // namespace section_predicate_cases

namespace conformance_section_close_cases {

// A condition in an embedded grammar reads nothing past the section, however
// the host spells the token that closes it: `lit<"a"> + &lit<"]">` refuses
// `[a]`, `lit<"a"> + &eof` reads it.

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

void expect(const Pair& pair, std::string_view text, bool want) {
  for (unsigned i = 0; i < 2; ++i) {
    const std::string source = pair.source(text);
    const auto r = pair[i]->parse(source);
    const bool got = r.accepted && r.diagnostics.empty() &&
                     r.error == cursiv::AstBuildError::none;
    if (got != want)
      std::printf("%s [%.*s]: wanted %s\n", kEngine[i],
                  static_cast<int>(text.size()), text.data(),
                  want ? "read" : "refused");
    CHECK(got == want);
  }
}

}  // namespace

void run() {
  namespace g = tconformance::generated;
  cursiv::test::count_expectations();

  const Pair atClose{g::Ll::create_parser(), g::Lr::create_parser(), "atclosehost:"};
  expect(atClose, "[a]", false);

  const Pair namedClose{g::Ll::create_parser(), g::Lr::create_parser(), "namedclosehost:"};
  expect(namedClose, "[a]", false);

  const Pair atEnd{g::Ll::create_parser(), g::Lr::create_parser(), "atendhost:"};
  expect(atEnd, "[a]", true);

  const Pair namedEnd{g::Ll::create_parser(), g::Lr::create_parser(), "namedendhost:"};
  expect(namedEnd, "[a]", true);

  const Pair bare{g::Ll::create_parser(), g::Lr::create_parser(), "barehost:"};
  expect(bare, "[a]", true);

  const Pair absentClose{g::Ll::create_parser(), g::Lr::create_parser(), "absentclosehost:"};
  expect(absentClose, "[a]", true);

  const Pair followed{g::Ll::create_parser(), g::Lr::create_parser(), "followedhost:"};
  expect(followed, "[a];", true);
  expect(followed, "[a]", false);

}

}  // namespace conformance_section_close_cases

int main() {
  cursiv::test::count_expectations();
  section_predicate_cases::run();
  conformance_section_close_cases::run();
  return cursiv::test::report("SectionPredicate_test");
}
