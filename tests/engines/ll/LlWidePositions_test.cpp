// A record's state holds the position that read it, and past fourteen bits
// the width comes out of the row's. Build that long program as data, keeping
// high states and the real completion runtime without compiling 16500 positions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/engines/ll/runtime/ll_caret.hpp"
#include "cursiv/engines/shared/gen/stamp_split.hpp"
#include "cursiv/engines/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/engines/shared/runtime/StampedGrammar.hpp"
#include "cursiv/test/Check.hpp"
#include "engines/ll/WidePositionsGrammar.hpp"
#include "wideposll/generated/Ll/Grammar.hpp"
#include "wideposll/generated/Ll/Module.hpp"
#include "wideposll/generated/Ll/Table.hpp"

namespace {

constexpr const auto& kLexer = twidepos::generated::Ll::kTable;
constexpr std::size_t kPositions = twidepos::kLeading + 2;
constexpr auto kStamp = *cursiv::stamp_split(0, kPositions + 1);
static_assert(kStamp.rowBits == 15);

constexpr std::uint16_t terminal(std::string_view spelling) {
  for (std::uint16_t t = 1; t < kLexer.nterms; ++t)
    if (kLexer.term_text(t) == spelling) return t;
  return cursiv::kInvalidSymbol;
}
constexpr auto kA = terminal("a"), kB = terminal("b"), kC = terminal("c");
static_assert(kA != cursiv::kInvalidSymbol && kB != cursiv::kInvalidSymbol &&
              kC != cursiv::kInvalidSymbol);

constexpr std::uint16_t token_at(std::size_t i) {
  return i < twidepos::kLeading ? kA : i == twidepos::kLeading ? kB : kC;
}

constexpr auto kNodes = [] {
  std::array<cursiv::LlTable::Node, kPositions> nodes{};
  for (std::size_t i = 0; i < nodes.size(); ++i)
    nodes[i] = {.mode = kLexer.errMode, .term = token_at(i)};
  return nodes;
}();
constexpr auto kStates = [] {
  std::array<cursiv::LlAtnState, kPositions + 1> states{};
  for (std::uint32_t i = 0; i < kPositions; ++i)
    states[i] = {.rule = 0, .edge = i, .nedges = 1};
  states.back() = {.kind = cursiv::LlAtnKind::ruleStop, .rule = 0};
  return states;
}();
constexpr auto kEdges = [] {
  std::array<cursiv::LlAtnEdge, kPositions> edges{};
  for (std::uint32_t i = 0; i < edges.size(); ++i)
    edges[i] = {.kind = cursiv::LlEdgeKind::atom,
                .label = token_at(i), .target = i + 1};
  return edges;
}();
constexpr auto kAfter = [] {
  std::array<std::uint32_t, kPositions> after{};
  for (std::uint32_t i = 0; i < after.size(); ++i) after[i] = i + 1;
  return after;
}();
constexpr auto kEdgeNodes = [] {
  std::array<std::uint16_t, kPositions> nodes{};
  for (std::uint16_t i = 0; i < nodes.size(); ++i) nodes[i] = i;
  return nodes;
}();
constexpr auto kReturnNodes = [] {
  std::array<std::uint16_t, kPositions + 1> nodes{};
  nodes.fill(cursiv::kNoRow);
  return nodes;
}();
constexpr std::uint32_t kRuleStart[] = {0};
constexpr std::uint32_t kCallerBase[] = {0, 0};

constexpr auto kTable = [] {
  auto table = kLexer;
  table.nodes = kNodes.data();
  table.nodeAfter = kAfter.data();
  table.nodeAfterGap = nullptr;
  table.nafter = kPositions;
  table.edgeNode = kEdgeNodes.data();
  table.returnNode = kReturnNodes.data();
  table.atnStart = 0;
  table.atn = {.states = kStates.data(), .nstates = kStates.size(),
               .edges = kEdges.data(), .ruleStart = kRuleStart,
               .callerBase = kCallerBase};
  table.stamp = kStamp;
  return table;
}();

std::vector<std::string> offered(const cursiv::StampedGrammar& grammar,
                                 const cursiv::Cst& tree, std::size_t at) {
  cursiv::LexSuffixSession<cursiv::LlLexTape<kTable>> session(
      tree.text().data(), tree.text().size());
  std::vector<cursiv::NextFeature> next;
  const auto where = static_cast<cursiv::TextOffset>(at);
  cursiv::ll_next_features_at<kTable>(grammar, tree, where, where, next);
  std::vector<std::string> spellings;
  for (const cursiv::NextFeature& f : next) {
    const auto spelled = grammar.terminalSpellings(f.feature);
    if (spelled.empty()) spellings.emplace_back();
    for (const std::string_view s : spelled) spellings.emplace_back(s);
  }
  return spellings;
}

void the_caret_far_down_a_long_rule_is_offered_what_it_owes() {
  const auto parser = twidepos::generated::Ll::create_parser();
  const std::string text = std::string(twidepos::kLeading, 'a') + "bc";
  const cursiv::ParseResult parsed = parser->parse(text);
  CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);

  const cursiv::StampedGrammar grammar(twidepos::generated::Ll::kGrammarMetaData,
                                        kStamp);
  cursiv::Cst tree(text);
  cursiv::CstBuilder builder(tree);
  builder.bind_stream();
  const auto open = builder.enter();
  for (std::uint16_t i = 0; i < kPositions; ++i)
    builder.leaf(i, i + 1, kStamp.make(cursiv::kNoElement, i + 1));
  const auto root = builder.exit(open, 0, kStamp.make(cursiv::kNoElement, 0));
  builder.bind_entry(0, tree.size(), root);
  builder.bind_accepted(true);
  CHECK(kStamp.state(tree[16382]) == 16383);
  CHECK(kStamp.state(tree[16383]) == 16384);
  CHECK(kStamp.state(tree[16384]) == 16385);

  for (const std::size_t at :
       {std::size_t{100}, std::size_t{16383}, std::size_t{16384},
        std::size_t{16400}, twidepos::kLeading - 1})
    CHECK(offered(grammar, tree, at) == std::vector<std::string>{"a"});
  CHECK(offered(grammar, tree, twidepos::kLeading) ==
        std::vector<std::string>{"b"});
  CHECK(offered(grammar, tree, twidepos::kLeading + 1) ==
        std::vector<std::string>{"c"});
}

}  // namespace

int main() {
  the_caret_far_down_a_long_rule_is_offered_what_it_owes();
  return cursiv::test::report("LlWidePositions_test");
}
