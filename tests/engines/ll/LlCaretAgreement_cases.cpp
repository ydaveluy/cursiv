#include <string>
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/ll/runtime/ll_caret.hpp"
#include "cursiv/Check.hpp"

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

// The entries of the identifier at the head of `text`, behind its keyword.
std::vector<Feature> identifiers_of(const cursiv::Parser& parser,
                                    const std::string& source,
                                    const cursiv::ParseResult& parsed) {
  const auto at = static_cast<cursiv::TextOffset>(source.find(' ') + 1u);
  std::vector<Feature> found;
  for (const Feature& f : features_at(parser, parsed.cst(), at))
    if (f.terminal == "ID") found.push_back(f);
  return found;
}

// Readings of one token each name their own member: the class, the member and
// what the site accepts tell two apart, and sites the same are one entry.
void each_reading_names_its_own_member() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  struct Case {
    std::string_view head;
    std::size_t entries;
    bool narrowed;
  };
  for (const Case& k : {Case{"same", 1, false}, Case{"filtered", 1, true},
                        Case{"alpha", 1, false}, Case{"beta", 1, false},
                        Case{"use", 2, false}, Case{"via", 2, false},
                        Case{"field", 2, false}, Case{"narrow", 2, true}}) {
    for (const std::string_view end : {" target+", " target-"}) {
      if ((k.head == "alpha" && end == " target-") ||
          (k.head == "beta" && end == " target+"))
        continue;
      const std::string source =
          "agree:" + std::string(k.head) + std::string(end);
      const auto parsed = ll->parse(source);
      CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);
      const auto found = identifiers_of(*ll, source, parsed);
      CHECK(found.size() == k.entries);
      for (const Feature& f : found) {
        CHECK(!f.property.empty() && f.type != nullptr);
        CHECK((f.narrow != cursiv::kNoNarrow) == k.narrowed);
      }
    }
  }
}

void a_narrowed_site_keeps_its_own_entry() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  const std::string source = "agree:mixed target+";
  const auto parsed = ll->parse(source);
  CHECK(parsed.accepted && parsed.diagnostics.empty());
  const auto found = identifiers_of(*ll, source, parsed);
  CHECK(found.size() == 2);
  if (found.size() == 2)
    CHECK((found[0].narrow != cursiv::kNoNarrow) !=
          (found[1].narrow != cursiv::kNoNarrow));
}

void rules_sharing_a_prefix_each_name_their_member() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  const std::string source = "agree:shared x : ";
  const auto parsed = ll->parse(source);
  std::vector<std::string_view> names;
  for (const Feature& f :
       features_at(*ll, parsed.cst(),
                   static_cast<cursiv::TextOffset>(source.size())))
    if (f.terminal == "ID") names.push_back(f.property);
  std::sort(names.begin(), names.end());
  CHECK(names == (std::vector<std::string_view>{"other", "to"}));
}

void a_reading_naming_nothing_leaves_the_member() {
  const auto ll = tllsemantic::generated::Ll::create_parser();
  for (const std::string_view text : {"bare target+", "bare target-"}) {
    const std::string source = "agree:" + std::string(text);
    const auto parsed = ll->parse(source);
    CHECK(parsed.accepted && parsed.diagnostics.empty() && parsed.value);
    const auto found = identifiers_of(*ll, source, parsed);
    CHECK(found.size() == 1);
    if (found.size() == 1)
      CHECK(found[0].property == "to" && found[0].type != nullptr);
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
    cursiv::detail::CstBuilder builder(tree);
    if (withLeaf) {
      const auto opened = builder.enter();
      builder.leaf(0, text.size(), 0);
      builder.exit(opened, 0, 0);
    }
    CHECK(!tree.engineStamped());
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
  std::vector<std::uint16_t> expected(table.atn.nstates,
                                      cursiv::engine::kNoRow);
  for (std::uint32_t state = 0; state < table.atn.nstates; ++state) {
    const auto& q = table.atn.state(state);
    for (std::uint32_t i = 0; i < q.nedges; ++i) {
      const auto& edge = table.atn.edge(q.edge + i);
      if (edge.kind == cursiv::engine::LlEdgeKind::rule)
        expected[edge.follow] = table.edgeNode[q.edge + i];
    }
  }
  for (std::uint32_t ret = 0; ret < table.atn.nstates; ++ret)
    CHECK(cursiv::engine::ll_call_at<tllsemantic::generated::Ll::kTable>(ret) ==
          expected[ret]);
  CHECK(cursiv::engine::ll_call_at<tllsemantic::generated::Ll::kTable>(
            table.atn.nstates) == cursiv::engine::kNoRow);
  CHECK(cursiv::engine::ll_call_at<tllsemantic::generated::Ll::kTable>(
            UINT32_MAX) == cursiv::engine::kNoRow);
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
  each_reading_names_its_own_member();
  a_narrowed_site_keeps_its_own_entry();
  rules_sharing_a_prefix_each_name_their_member();
  a_reading_naming_nothing_leaves_the_member();
  an_unstamped_tree_does_not_supply_engine_positions();
  generated_return_nodes_match_the_call_graph();
}

}  // namespace ll_caret_agreement_cases
