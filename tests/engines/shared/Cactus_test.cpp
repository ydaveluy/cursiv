#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <tuple>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/engine/parser/Explorer.hpp"

namespace {

using namespace cursiv;
using namespace cursiv::engine;

void the_first_shift_has_a_derivation_before_clear() {
  Cactus stacks;
  const auto leaf = stacks.push(1, kNoStack, 1, 1, 1, {}, 0);
  const auto& way = stacks.way(stacks.ways(leaf.node));
  CHECK(!leaf.shared && way.production == kNoProduction && way.arm == kNoStack && way.next == kNoStack);
}

void a_node_stands_for_its_state_parent_place_and_token() {
  const std::array<Frame, 5> frames{{{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}}};
  Cactus graph;
  graph.clear(frames);
  using Key = std::tuple<std::uint32_t, std::uint32_t, TextOffset, std::uint64_t, TextOffset>;
  std::map<Key, std::uint32_t> known;
  std::mt19937 random(20261118);
  for (unsigned i = 0; i < 40000; ++i) {
    const TextOffset end = 8 * (random() % 32);
    const std::uint32_t state = random() % 256;
    const std::uint32_t parent = random() % frames.size();
    const unsigned held = random() % 4;
    const std::uint64_t head = held == 0 ? 0 : std::uint64_t{end + held} << 32 | held;
    const TextOffset until = held == 0 ? Cactus::kNoHeldEnd : end + 16;
    const std::uint32_t node = graph.push(state, parent, end, head, until, {}, 0).node;
    const auto [at, made] = known.try_emplace(Key{state, parent, end, head, until}, node);
    CHECK(at->second == node && graph.size() == known.size());
    CHECK(graph.state(node) == state && graph.parent(node) == parent && graph.end(node) == end);
  }
}

void a_state_over_many_parents_makes_a_node_each() {
  const std::array<Frame, 1> frames{{{0, 0}}};
  Cactus graph;
  graph.clear(frames);
  std::vector<std::uint32_t> parents, nodes;
  for (std::uint32_t i = 0; i < 4096; ++i)
    parents.push_back(graph.push(1000 + i, 0, 8, 0, Cactus::kNoHeldEnd, {}, 0).node);
  for (const std::uint32_t parent : parents) {
    nodes.push_back(graph.push(7, parent, 16, 0, Cactus::kNoHeldEnd, {}, 0).node);
    CHECK(graph.parent(nodes.back()) == parent);
  }
  CHECK(graph.size() == 2 * parents.size());
  for (std::size_t i = 0; i < parents.size(); ++i)
    CHECK(graph.push(7, parents[i], 16, 0, Cactus::kNoHeldEnd, {}, 0).node == nodes[i]);
  CHECK(graph.size() == 2 * parents.size());
}

void a_node_keeps_each_distinct_way_once() {
  const std::array<Frame, 1> frames{{{0, 0}}};
  Cactus graph;
  graph.clear(frames);
  const std::uint32_t node = graph.push(5, 0, 4, 0, Cactus::kNoHeldEnd, {.production = 1}, 0).node;
  CHECK(graph.push(5, 0, 4, 0, Cactus::kNoHeldEnd, {.production = 2}, 0).node == node);
  CHECK(graph.push(5, 0, 4, 0, Cactus::kNoHeldEnd, {.production = 1}, 0).node == node);
  CHECK(graph.size() == 1 && graph.revision() == 1);
  std::size_t count = 0;
  for (std::uint32_t at = graph.ways(node); at != kNoStack; at = graph.way(at).next) ++count;
  CHECK(count == 2);
  const auto chosen = graph.ways(node);
  graph.settle(node, chosen);
  CHECK(graph.push(5, 0, 4, 0, Cactus::kNoHeldEnd, {.production = 1}, 0).shared);
  CHECK(graph.way(graph.ways(node)).next == chosen);
}

// Parents are outside the key compared when two forest nodes may be aliases.
void unit_productions_are_read_from_each_nodes_parent() {
  const std::array<Frame, 3> frames{{{0, 0}, {1, 0}, {2, 0}}};
  const std::uint32_t sizes[] = {0}, symbols[] = {91};
  const std::uint32_t rows[] = {1, 2, 3}, offsets[] = {0, 2, 4, 7};
  const auto entry = [](unsigned symbol, unsigned hops) { return std::uint64_t{symbol} << 32 | hops; };
  const std::uint64_t words[] = {entry(7, 1), entry(91, 3), entry(7, 2), entry(91, 4),
                                 entry(7, 1), entry(20, 2), entry(91, 3)};
  Tables t;
  t.productionSize = sizes;
  t.productionSymbol = symbols;
  t.derivationOf = rows;
  t.derivationAt = offsets;
  t.derivationWord = words;
  Cactus graph;
  for (const auto production : {kNoProduction, 0u}) {
    graph.clear(frames);
    const auto push = [&](unsigned parent) {
      return graph.push(8, parent, 1, 7, 1, {.production = production}, 0).node;
    };
    const auto first = push(0), different = push(1), alike = push(2);
    CHECK(push(0) == first && graph.way(graph.ways(first)).next == kNoStack);
    CHECK(graph.same(first, alike, t, kNoCancel) && !graph.same(first, different, t, kNoCancel));
    CHECK(graph.hops(t, first, graph.way(graph.ways(first))) == (production == kNoProduction ? 1u : 3u));
    graph.alias(first, different, t, kNoCancel);
    CHECK(graph.forks(first));
  }
}

void clearing_switches_between_graphs_and_canonical_stacks() {
  const std::array<Frame, 3> frames{{{0, 0}, {1, 0}, {2, 0}}};
  Cactus stacks;
  for (unsigned round = 0; round < 4; ++round) {
    stacks.clear(frames);
    for (unsigned state = 0; state < 256; ++state) {
      const auto node = stacks.intern(state, state % frames.size());
      CHECK(stacks.intern(state, state % frames.size()) == node);
      CHECK(stacks.parent(node) == state % frames.size());
      CHECK(stacks.depth(node) == state % frames.size() + 2);
    }
    stacks.clear(frames);
    for (unsigned end : {48u, 16u, 32u, 0u, 64u})
      for (unsigned state = 0; state < 96; ++state) {
        const auto node = stacks.push(state, 0, end, 0, Cactus::kNoHeldEnd, {}, 0).node;
        CHECK(stacks.push(state, 0, end, 0, Cactus::kNoHeldEnd, {}, 0).node == node);
      }
  }
}

}  // namespace

int main() {
  the_first_shift_has_a_derivation_before_clear();
  a_node_stands_for_its_state_parent_place_and_token();
  a_state_over_many_parents_makes_a_node_each();
  a_node_keeps_each_distinct_way_once();
  unit_productions_are_read_from_each_nodes_parent();
  clearing_switches_between_graphs_and_canonical_stacks();
  return cursiv::test::report("Cactus_test");
}
