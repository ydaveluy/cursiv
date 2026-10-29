#include <string>
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstBuilder.hpp"
#include "cursiv/engines/ll/runtime/ll_caret.hpp"
#include "cursiv/test/Check.hpp"

#include "semanticll/generated/Ll/Module.hpp"
#include "semanticll/generated/Ll/Table.hpp"

namespace ll_caret_agreement_cases {

namespace {

struct Feature {
  std::string_view terminal;
  std::vector<std::string_view> spellings;
  std::string_view property;
  std::uint8_t narrow;
  const cursiv::AstTypeTag* type;
  bool operator==(const Feature&) const = default;
};

std::vector<Feature> features_at(const cursiv::Parser& parser,
                                 const cursiv::Cst& tree,
                                 cursiv::TextOffset at) {
  std::vector<cursiv::NextFeature> next;
  parser.nextFeaturesAt(tree, at, at, next);
  std::vector<Feature> out;
  for (const auto& feature : next) {
    const auto spellings =
        parser.getGrammar().terminalSpellings(feature.feature);
    out.push_back({parser.getGrammar().terminalName(feature.feature),
                   {spellings.begin(), spellings.end()},
                   feature.property,
                   feature.narrow,
                   feature.type});
  }
  const auto key = [](const Feature& feature) {
    return std::tuple{feature.terminal, feature.spellings, feature.property,
                      feature.narrow,
                      feature.type ? feature.type->name : std::string_view{}};
  };
  std::sort(out.begin(), out.end(),
            [&](const auto& a, const auto& b) { return key(a) < key(b); });
  return out;
}

void every_reading_must_agree_on_the_complete_metadata() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  for (const std::string_view text :
       {"use target+", "use target-", "via target+", "via target-",
        "narrow target+", "narrow target-", "mixed target+", "mixed target-",
        "field target+", "field target-", "same target+", "same target-",
        "filtered target+", "filtered target-", "alpha target+",
        "beta target-"}) {
    const std::string source = "agree:" + std::string(text);
    const auto b = ll->parse(source);
    CHECK(b.accepted && b.diagnostics.empty() && b.value);
    const auto at = static_cast<cursiv::TextOffset>(source.find(' ') + 1u);
    const auto actual = features_at(*ll, b.cst(), at);
    const auto found =
        std::find_if(actual.begin(), actual.end(),
                     [](const auto& f) { return f.terminal == "ID"; });
    CHECK(found != actual.end());
    if (found == actual.end()) continue;
    const auto head = text.substr(0, at - 7u);
    const bool named = head == "same" || head == "filtered" ||
                       head == "alpha" || head == "beta";
    if (named) {
      CHECK(found->property == "to" && found->type == b.value->type);
      CHECK((found->narrow != cursiv::kNoNarrow) == (head == "filtered"));
    } else {
      CHECK(found->property.empty() && found->type == nullptr &&
            found->narrow == cursiv::kNoNarrow);
    }
  }
}

void an_unnamed_reading_prevents_member_attribution() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  for (const std::string_view text : {"bare target+", "bare target-"}) {
    const std::string source = "agree:" + std::string(text);
    const auto parsed = ll->parse(source);
    CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);
    const auto features = features_at(*ll, parsed.cst(), 11);
    const auto found =
        std::find_if(features.begin(), features.end(),
                     [](const auto& f) { return f.terminal == "ID"; });
    CHECK(found != features.end());
    if (found != features.end())
      CHECK(found->property.empty() && found->type == nullptr &&
            found->narrow == cursiv::kNoNarrow);
  }
}

void an_unstamped_tree_does_not_supply_engine_positions() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  cursiv::Cst empty("");
  std::vector<cursiv::NextFeature> none;
  ll->nextFeaturesAt(empty, 0, 0, none);
  CHECK(none.empty());
  for (const bool withLeaf : {false, true}) {
    constexpr std::string_view text = "use target+";
    cursiv::Cst tree(text);
    cursiv::CstBuilder builder(tree);
    if (withLeaf) {
      const auto opened = builder.enter();
      builder.leaf(0, text.size(), 0);
      builder.exit(opened, 0, 0);
    }
    CHECK(!tree.engine_stamped());
    for (cursiv::TextOffset at = 0; at <= text.size(); ++at) {
      std::vector<cursiv::NextFeature> output{{123u, "kept", 7u, nullptr}};
      const auto* data = output.data();
      const auto capacity = output.capacity();
      ll->nextFeaturesAt(tree, at, at, output);
      CHECK(output.size() == 1 && output[0].feature == 123u &&
            output[0].property == "kept" && output[0].narrow == 7u &&
            output[0].type == nullptr);
      CHECK(output.data() == data && output.capacity() == capacity);
    }
  }
}

void generated_return_nodes_match_the_call_graph() {
  const auto& table = tllsemantic::generated::Ll::kTable;
  std::vector<std::uint16_t> expected(table.atn.nstates, cursiv::kNoRow);
  for (std::uint32_t state = 0; state < table.atn.nstates; ++state) {
    const auto& q = table.atn.state(state);
    for (std::uint32_t i = 0; i < q.nedges; ++i) {
      const auto& edge = table.atn.edge(q.edge + i);
      if (edge.kind == cursiv::LlEdgeKind::rule)
        expected[edge.follow] = table.edgeNode[q.edge + i];
    }
  }
  for (std::uint32_t ret = 0; ret < table.atn.nstates; ++ret)
    CHECK(cursiv::ll_call_at<tllsemantic::generated::Ll::kTable>(ret) ==
          expected[ret]);
  CHECK(cursiv::ll_call_at<tllsemantic::generated::Ll::kTable>(
            table.atn.nstates) == cursiv::kNoRow);
  CHECK(cursiv::ll_call_at<tllsemantic::generated::Ll::kTable>(UINT32_MAX) ==
        cursiv::kNoRow);
}

struct GuardCase { std::string text; std::string_view member; bool narrowed; };
void the_guarded_reference_keeps_its_member() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  for (const GuardCase& k :
       {GuardCase{"link target+", "to", false}, GuardCase{"link target-", "other", false},
        GuardCase{"link  target+", "to", false},
        GuardCase{"link  target-", "other", false},
        GuardCase{"direct target+", "to", false},
        GuardCase{"direct target-", "other", false},
        GuardCase{"narrow target+", "to", true},
        GuardCase{"narrow target-", "other", true}, GuardCase{"ka target+", "to", false},
        GuardCase{"kb target-", "other", false}}) {
    const auto at = static_cast<cursiv::TextOffset>(k.text.find("target"));
    const std::string source = "guard:" + k.text;
    const cursiv::ParseResult parsed = ll->parse(source);
    CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);
    const auto next = features_at(*ll, parsed.cst(), at + 6);
    std::vector<Feature> names;
    for (const auto& f : next)
      if (f.terminal == "ID") names.push_back(f);
    CHECK(names.size() == 1);
    if (names.size() != 1) continue;
    CHECK(names[0].property == k.member);
    CHECK(names[0].type == parsed.value->type);
    CHECK((names[0].narrow != cursiv::kNoNarrow) == k.narrowed);
  }
}

}  // namespace

void run() {
  the_guarded_reference_keeps_its_member();
  every_reading_must_agree_on_the_complete_metadata();
  an_unnamed_reading_prevents_member_attribution();
  an_unstamped_tree_does_not_supply_engine_positions();
  generated_return_nodes_match_the_call_graph();
}

}  // namespace ll_caret_agreement_cases
