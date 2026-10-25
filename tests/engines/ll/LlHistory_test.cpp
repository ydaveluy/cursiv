#include "cursiv/engines/ll/runtime/LlPlan.hpp"
// Shared derivation histories against sequences compared the plain way.

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "cursiv/engines/ll/runtime/LlHistory.hpp"
#include "cursiv/test/Check.hpp"

namespace {

using Words = std::vector<std::uint32_t>;

Words words_of(const cursiv::LlHistory& h, cursiv::LlHistory::Node n) {
  Words out;
  for (; n != cursiv::LlHistory::kRoot; n = h.parent(n))
    out.push_back(h.word(n));
  std::reverse(out.begin(), out.end());
  return out;
}

void grafts_keep_canonical_prefixes_and_positions() {
  using History = cursiv::LlHistory;
  for (const unsigned length : {31u, 32u, 33u, 63u, 64u, 65u}) {
    History h;
    std::vector<History::Node> original{History::kRoot};
    for (unsigned i = 0; i < length; ++i)
      original.push_back(h.append(original.back(), i % 7u, i + 100u, i % 3u));
    for (const unsigned cut : {1u, 7u, 16u}) {
      for (const unsigned baseLength : {cut - 1u, cut, cut + 1u}) {
        auto base = History::kRoot;
        for (unsigned i = 0; i < baseLength; ++i)
          base = h.append(base, 9u, i + 1000u, i % 2u);
        auto expected = base;
        std::vector<History::Node> prefixes{base};
        for (unsigned i = cut; i < length; ++i) {
          expected = h.append(expected, i % 7u, i + 100u, i % 3u);
          prefixes.push_back(expected);
        }
        const auto made = h.graft(base, original[cut], original.back());
        CHECK(made == expected);
        CHECK(h.graft(base, original[cut], original[cut]) == base);
        CHECK(h.graft(original[cut], original[cut], original.back()) ==
              original.back());
        for (unsigned i = 0; i < prefixes.size(); ++i)
          CHECK(h.ancestor(made, baseLength + i) == prefixes[i]);
        auto n = made;
        for (std::size_t i = prefixes.size(); i-- > 1;) {
          CHECK(h.word(n) == (cut + i - 1u) % 7u);
          CHECK(h.at(n) == cut + i - 1u + 100u);
          CHECK(h.lull(n) == (cut + i - 1u) % 3u);
          n = h.parent(n);
          CHECK(n == prefixes[i - 1u]);
        }
        CHECK(n == base);
        CHECK(h.common(made, original.back()) == History::kRoot);
      }
    }
    // A `from` that is not an ancestor of `to` lays all of `to` after `base`.
    const auto other = h.append(History::kRoot, 88u, 1u, 2u);
    auto expected = other;
    for (unsigned i = 0; i < length; ++i)
      expected = h.append(expected, i % 7u, i + 100u, i % 3u);
    CHECK(h.graft(other, other, original.back()) == expected);
  }
}

void long_continuations_share_their_blocks() {
  using History = cursiv::LlHistory;
  History h;
  const auto from = h.append(History::kRoot, 7u, 1u);
  auto tail = from;
  constexpr unsigned length = 16384, alternatives = 256;
  for (unsigned i = 0; i < length; ++i)
    tail = h.append(tail, i % 7u, i + 100u, i % 3u);
  const auto before = h.size();
  for (unsigned i = 0; i < alternatives; ++i) {
    const auto base = h.append(History::kRoot, 9u, i + 100000u);
    const auto made = h.graft(base, from, tail);
    CHECK(h.depth(made) == length + 1u);
    CHECK(h.ancestor(made, 1u) == base);
    CHECK(h.word(made) == h.word(tail));
    CHECK(h.at(made) == h.at(tail) && h.lull(made) == h.lull(tail));
  }
  // A long continuation costs blocks at its boundary, not a copy of every
  // event for each alternative prefix.
  CHECK(h.size() - before < alternatives * 128u);
}

void shared_continuations_keep_the_length_in_range() {
  cursiv::LlHistory h;
  auto node = h.append(cursiv::LlHistory::kRoot, 1u);
  for (unsigned i = 0; i < 31; ++i)
    node = h.graft(node, cursiv::LlHistory::kRoot, node);
  CHECK(h.depth(node) == (std::uint32_t{1} << 31));
  bool refused = false;
  try {
    (void)h.graft(node, cursiv::LlHistory::kRoot, node);
  } catch (const std::length_error&) {
    refused = true;
  }
  CHECK(refused);
}

}  // namespace

void a_repair_invalidates_the_prediction_plan() {
  using cursiv::kLlUnplanned;
  cursiv::LlPlan plan;
  plan.entries() = {
      {.at = 4, .decision = 7, .arm = 1},
      {.at = 4, .decision = 7, .arm = 0},
      {.at = 4, .lull = 1, .decision = 7, .arm = 2},
      {.at = 9, .decision = 3, .arm = cursiv::kNoRow},
  };
  plan.hold(0);
  CHECK(plan.say(7, 4, 0, 0, false) == 1);
  CHECK(plan.say(7, 4, 0, 0, false) == 0);
  CHECK(plan.say(7, 4, 0, 0, false) == kLlUnplanned);
  CHECK(plan.say(7, 4, 1, 0, false) == 2);
  CHECK(plan.say(7, 5, 0, 0, false) == kLlUnplanned);
  CHECK(plan.say(8, 4, 0, 0, false) == kLlUnplanned);
  CHECK(plan.say(3, 9, 0, 0, false) == cursiv::kNoRow);
  CHECK(plan.say(3, 9, 0, 0, false) == cursiv::kNoRow);
  plan.entries().push_back({.at = 12, .decision = 1, .arm = 1});
  CHECK(plan.say(1, 12, 0, 1, false) == kLlUnplanned);
  plan.entries().push_back({.at = 15, .decision = 5, .arm = 3});
  CHECK(plan.say(5, 15, 0, 0, true) == 3);
  CHECK(plan.say(5, 15, 0, 0, true) == 3);
  CHECK(plan.say(5, 15, 0, 0, false) == 3);
  CHECK(plan.say(5, 15, 0, 0, false) == kLlUnplanned);
  }

int main() {
  a_repair_invalidates_the_prediction_plan();
  std::mt19937 rng(7);
  cursiv::LlHistory h;
  std::vector<cursiv::LlHistory::Node> nodes{cursiv::LlHistory::kRoot};
  for (unsigned i = 0; i < 20000; ++i) {
    const auto parent = nodes[rng() % nodes.size()];
    const auto at = h.append(parent, rng() % 4);
    CHECK(h.append(parent, h.word(at)) == at);
    nodes.push_back(at);
  }
  CHECK(h.depth(nodes.back()) == words_of(h, nodes.back()).size());
  for (unsigned i = 0; i < 20000; ++i) {
    const auto a = nodes[rng() % nodes.size()];
    const auto b = nodes[rng() % nodes.size()];
    const Words x = words_of(h, a), y = words_of(h, b);
    CHECK(h.before(a, b) == (a != b && x < y));
    const auto c = h.common(a, b);
    const Words z = words_of(h, c);
    CHECK(std::equal(z.begin(), z.end(), x.begin()) && z.size() <= x.size() &&
          std::equal(z.begin(), z.end(), y.begin()) && z.size() <= y.size());
    if (z.size() < x.size() && z.size() < y.size())
      CHECK(x[z.size()] != y[z.size()]);
  }
  // One word at two places, or after two runs of tokens of no width, is two
  // nodes.
  const auto here = h.append(cursiv::LlHistory::kRoot, 5, 10, 0);
  const auto later = h.append(cursiv::LlHistory::kRoot, 5, 10, 1);
  const auto elsewhere = h.append(cursiv::LlHistory::kRoot, 5, 11, 0);
  CHECK(here != later && here != elsewhere && later != elsewhere);
  CHECK(h.append(cursiv::LlHistory::kRoot, 5, 10, 1) == later);
  CHECK(h.at(later) == 10 && h.lull(later) == 1);
  CHECK(!h.before(here, later) && !h.before(later, here));
  // A long chain stays logarithmic to climb.
  auto tip = cursiv::LlHistory::kRoot;
  for (unsigned i = 0; i < 200000; ++i) tip = h.append(tip, 1);
  CHECK(h.depth(h.ancestor(tip, 777)) == 777);
  grafts_keep_canonical_prefixes_and_positions();
  long_continuations_share_their_blocks();
  shared_continuations_keep_the_length_in_range();
  return cursiv::test::report("LlHistory_test");
}
