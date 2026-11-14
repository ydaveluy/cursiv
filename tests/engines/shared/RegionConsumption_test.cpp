#include <algorithm>
#include <cstdio>
#include <string_view>
#include <vector>

#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/Check.hpp"
#include "engines/shared/RegionBoundaryGrammar.hpp"
#include "regionboundaryll/generated/Ll/Module.hpp"
#include "regionboundarylr/generated/Lr/Module.hpp"

namespace {
struct Reading {
  const cursiv::Parser& parser;
  const char* engine;
};

bool clean(const cursiv::ParseResult& result) {
  return result.accepted && !result.cst().recovered() &&
         result.diagnostics.empty();
}

void accepts(const Reading& reading, std::string_view source,
             std::size_t count) {
  const auto result = reading.parser.parse(source);
  const auto* doc = result.valueAs<tregionconsumption::Document>();
  if (!clean(result))
    std::fprintf(stderr, "region consumption [%s]: expected clean %.*s\n",
                 reading.engine, int(source.size()), source.data());
  CHECK(clean(result));
  CHECK(doc && !doc->parent && result.nodes().size() == 2);
  CHECK(doc && doc->block && doc->block->parent == doc);
  CHECK(doc && doc->block && doc->block->items.size() == count);
  CHECK(result.cst().text() == source);
  if (doc && doc->block) {
    for (const auto item : doc->block->items) CHECK(item == "x");
    CHECK(doc->cstNodeId < result.cst().size());
    CHECK(doc->block->cstNodeId < result.cst().size());
    if (doc->cstNodeId < result.cst().size() &&
        doc->block->cstNodeId < result.cst().size()) {
      const auto& outer = result.cst()[doc->cstNodeId];
      const auto& inner = result.cst()[doc->block->cstNodeId];
      CHECK(outer.begin <= inner.begin && inner.end <= outer.end);
    }
  }

  // Every note in these accepted cases is caller trivia. Keep its exact
  // byte range and text once, including notes after a nullable empty block.
  std::vector<cursiv::TextOffset> expected;
  for (std::size_t at = 0; at < source.size(); ++at)
    if (source[at] == '~')
      expected.push_back(static_cast<cursiv::TextOffset>(at));
  std::vector<cursiv::TextOffset> actual;
  for (cursiv::NodeId id = 0; id < result.cst().size(); ++id) {
    const auto& node = result.cst()[id];
    if (node.hidden && node.subtreeSize == 1) {
      CHECK(result.cst().text(id) == "~");
      CHECK(node.end == node.begin + 1);
      actual.push_back(node.begin);
    }
  }
  CHECK(actual == expected);
}

void rejects(const Reading& reading, std::string_view source) {
  const auto result = reading.parser.parse(source);
  if (clean(result))
    std::fprintf(stderr, "region consumption [%s]: unexpected clean %.*s\n",
                 reading.engine, int(source.size()), source.data());
  CHECK(!clean(result));
}

void owning_a_record_preserves_the_consuming_gap(const Reading& reading) {
  for (const std::string_view source :
       {"owned:[].", "owned:[ ].", "owned:[\n].", "owned:~[~]~."})
    accepts(reading, source, 0);
  for (const std::string_view source :
       {"owned:[x].", "owned:[x ].", "owned:[x\n].", "owned:~[x~]~."})
    accepts(reading, source, 1);
  for (const std::string_view source :
       {"owned:[ x].", "owned:[~x].", "owned:[\nx].", "owned:[xx].",
        "owned:[x x]."})
    rejects(reading, source);

  accepts(reading, "block-some:[x].", 1);
  accepts(reading, "block-some: [xx ] .", 2);
  accepts(reading, "block-some:~[xxx~]~.", 3);
  for (const std::string_view source :
       {"block-some:[].", "block-some:[ ].", "block-some:[ x].", "block-some:[x x].", "block-some:[x~x].",
        "block-some:[x\nx]."})
    rejects(reading, source);
}

void the_first_item_keeps_the_callers_gap(const Reading& reading) {
  accepts(reading, "entry: x].", 1);
  accepts(reading, "entry:\nxx ].", 2);
  accepts(reading, "entry:~xxx~]~.", 3);
  for (const std::string_view source :
       {"entry:].", "entry: ].", "entry:x x].", "entry:x~x].", "entry:x\nx]."})
    rejects(reading, source);

  accepts(reading, "maybe:].", 0);
  accepts(reading, "maybe: ].", 0);
  accepts(reading, "maybe:\nx].", 1);
  accepts(reading, "maybe:~x~]~.", 1);
  for (const std::string_view source : {"maybe:xx].", "maybe:x x]."})
    rejects(reading, source);

  accepts(reading, "many:].", 0);
  accepts(reading, "many: ].", 0);
  accepts(reading, "many: x].", 1);
  accepts(reading, "many:~xx~]~.", 2);
  for (const std::string_view source :
       {"many:x x].", "many:x~x].", "many:x\nx]."})
    rejects(reading, source);
}

void offers(const Reading& reading, std::string_view source,
            cursiv::TextOffset at, std::vector<std::string_view> expected) {
  const auto parsed = reading.parser.parse(source);
  CHECK(clean(parsed));
  std::vector<cursiv::NextFeature> next;
  reading.parser.nextFeaturesAt(parsed.cst(), at, at, next);
  std::vector<std::string_view> actual;
  for (const auto& feature : next) {
    const auto spellings =
        reading.parser.getGrammar().terminalSpellings(feature.feature);
    actual.push_back(spellings.size() == 1
                         ? spellings.front()
                         : reading.parser.getGrammar().terminalName(
                               feature.feature));
  }
  std::sort(actual.begin(), actual.end());
  actual.erase(std::unique(actual.begin(), actual.end()), actual.end());
  if (actual != expected) {
    std::fprintf(stderr, "region completion [%s]: %.*s at %u offers",
                 reading.engine, int(source.size()), source.data(),
                 unsigned(at));
    for (const auto text : actual)
      std::fprintf(stderr, " %.*s", int(text.size()), text.data());
    std::fputc('\n', stderr);
  }
  CHECK(actual == expected);
}

void completion_respects_the_committed_gap(const Reading& reading) {
  offers(reading, "owned:[ ].", 7, {"]", "x"});
  offers(reading, "owned:[ ].", 8, {"]"});
  offers(reading, "owned:[x ].", 9, {"]"});
}
}  // namespace

int main() {
  const auto ll = tregionboundary::generated::Ll::create_parser();
  const auto lr = tregionboundary::generated::Lr::create_parser();
  for (const Reading reading : {Reading{*ll, "LL"}, Reading{*lr, "LR"}}) {
    owning_a_record_preserves_the_consuming_gap(reading);
    the_first_item_keeps_the_callers_gap(reading);
    completion_respects_the_committed_gap(reading);
  }
  return cursiv::test::report("RegionConsumption_test");
}
