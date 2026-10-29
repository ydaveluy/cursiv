#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ConformanceTest.hpp"
#include "conformancell/generated/Ll/Module.hpp"
#include "conformancelr/generated/Lr/Module.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstBuildError.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/shared/CaretOffers.hpp"
#include "engines/shared/ConformanceGrammars.hpp"
#include "engines/shared/SectionPredicateGrammar.hpp"
#include "sectionpredicatell/generated/Ll/Module.hpp"
#include "sectionpredicatelr/generated/Lr/Module.hpp"

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
  const cursiv::Parser& parser;
  std::string_view prefix;

  std::string source(std::string_view text) const {
    return std::string(prefix) + std::string(text);
  }
};

std::string alone(const Reader& p, std::string_view text) {
  const std::string source = p.source(text);
  const auto r = p.parser.parse(source);
  return items(r.value_as<ast::Items>()) + diagnostics(r, p.prefix.size());
}

std::string embedded(const Reader& p, std::string_view text) {
  const std::string host = p.source('<' + std::string(text) + '>');
  const auto r = p.parser.parse(host);
  const auto* d = r.value_as<ast::Doc>();
  const std::string tree = d == nullptr || d->blocks.size() != 1
                               ? "-"
                               : items((*d->blocks.begin())->items);
  return tree + diagnostics(r, p.prefix.size() + 1);
}

void same(const Reader& one, const Reader& host, std::string_view text) {
  const auto a = alone(one, text);
  const auto b = embedded(host, text);
  if (a != b)
    std::printf("\"%.*s\": alone %s, embedded %s\n",
                static_cast<int>(text.size()), text.data(), a.c_str(),
                b.c_str());
  CHECK(a == b);
}

}  // namespace

void run() {
  namespace gen = tsecpred::generated;
  const auto ll = gen::Ll::create_parser();
  const auto lr = gen::Lr::create_parser();
  const Reader lastLl{*ll, ""};
  const Reader lastLr{*lr, ""};
  const Reader inLastLl{*ll, "inlast:"};
  const Reader inLastLr{*lr, "inlast:"};
  const Reader reachLl{*ll, "reach:"};
  const Reader reachLr{*lr, "reach:"};
  const Reader inReachLl{*ll, "inreach:"};
  const Reader inReachLr{*lr, "inreach:"};
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
  // These predicates retain their space-only policy, including the first gap.
  for (const Reader* reader : {&lastLl, &lastLr}) {
    for (const std::string_view text : {"\ta b", "\na b", "a\tb", "a\nb"}) {
      const std::string source = reader->source(text);
      const auto parsed = reader->parser.parse(source);
      CHECK(!parsed.accepted || !parsed.diagnostics.empty());
    }
  }
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
  // A predicate of the last word that reads to the caret is answered by what
  // is typed there: behind `x ` the word is the last one on the document's
  // text, and a word typed after it makes `x` an item before the last.
  for (const cursiv::Parser* parser : {ll.get(), lr.get()})
    CHECK((caret_offers::offered(*parser, "x ", 2) ==
           std::vector<std::string>{"ID"}));
}

}  // namespace section_predicate_cases

namespace conformance_section_close_cases {

// A condition in an embedded grammar reads nothing past the section, however
// the host spells the token that closes it: `lit<"a"> + &lit<"]">` refuses
// `[a]`, `lit<"a"> + &eof` reads it.

using conformance_test::expect;
using conformance_test::Pair;

void run() {
  cursiv::test::count_expectations();

  const Pair atClose{"atclosehost:"};
  expect(atClose, "[a]", false);

  const Pair namedClose{"namedclosehost:"};
  expect(namedClose, "[a]", false);

  const Pair atEnd{"atendhost:"};
  expect(atEnd, "[a]", true);

  const Pair namedEnd{"namedendhost:"};
  expect(namedEnd, "[a]", true);

  const Pair bare{"barehost:"};
  expect(bare, "[a]", true);

  const Pair absentClose{"absentclosehost:"};
  expect(absentClose, "[a]", true);

  const Pair followed{"followedhost:"};
  expect(followed, "[a];", true);
  expect(followed, "[a]", false);
}

}  // namespace conformance_section_close_cases
