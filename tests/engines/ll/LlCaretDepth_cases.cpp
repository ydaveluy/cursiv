// A caret answers the same at any depth: the stack a document builds is read
// whole, however far it grows.

#include <algorithm>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "semanticll/generated/Ll/Module.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"

namespace ll_caret_depth_cases {

namespace {

struct Entry {
  std::string spelling, property;
  bool operator<(const Entry& other) const {
    return std::tie(spelling, property) <
           std::tie(other.spelling, other.property);
  }
  bool operator==(const Entry&) const = default;
};

std::vector<Entry> features_at(const cursiv::Parser& parser,
                               const cursiv::ParseResult& parsed,
                               cursiv::TextOffset at) {
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(parsed.cst(), at, at, next);
  std::vector<Entry> out;
  for (const cursiv::NextFeature& f : next) {
    const std::string_view spelled =
        parser.getGrammar().terminalText(f.feature);
    out.push_back({std::string(spelled.empty()
                                   ? parser.getGrammar().terminalName(f.feature)
                                   : spelled),
                   std::string(f.property)});
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string parens(unsigned depth) {
  return "depth:expr " + std::string(depth, '(') + "a" + std::string(depth, ')');
}

std::string blocks(unsigned depth) {
  std::string text = "depth:block ";
  for (unsigned i = 0; i < depth; ++i) text += "{ x; ";
  text += std::string(depth, '}');
  return text;
}

void check_same(const cursiv::Parser& parser, const std::string& deep,
                cursiv::TextOffset deepAt, const std::string& shallow,
                cursiv::TextOffset shallowAt) {
  const auto a = parser.parse(deep);
  const auto b = parser.parse(shallow);
  CHECK(a.accepted && a.diagnostics.empty());
  CHECK(b.accepted && b.diagnostics.empty());
  const auto want = features_at(parser, b, shallowAt);
  const auto got = features_at(parser, a, deepAt);
  CHECK(!want.empty());
  CHECK(got == want);
}

void a_caret_deep_in_parentheses_names_the_outermost_member() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  for (const unsigned depth : {100u, 300u, 2000u}) {
    check_same(*ll, parens(depth), 11 + depth, parens(3), 11 + 3);
    check_same(*ll, parens(depth), 11 + depth / 2, parens(3), 11 + 1);
  }
}

void a_caret_deep_in_blocks_answers_as_a_shallow_one_does() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  for (const unsigned depth : {100u, 300u, 2000u})
    check_same(*ll, blocks(depth), 12 + 4 * depth, blocks(3), 12 + 4 * 3);
}

}  // namespace

void run() {
  a_caret_deep_in_parentheses_names_the_outermost_member();
  a_caret_deep_in_blocks_answers_as_a_shallow_one_does();
}

}  // namespace ll_caret_depth_cases
