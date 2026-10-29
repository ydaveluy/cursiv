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
  h.each(n, 0, [&](const cursiv::LlHistory::Event& e) {
    out.push_back(e.word);
    return true;
  });
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
          const auto last = h.last(n);
          CHECK(last.word == (cut + i - 1u) % 7u);
          CHECK(last.at == cut + i - 1u + 100u);
          CHECK(last.lull == (cut + i - 1u) % 3u);
          n = h.ancestor(n, h.depth(n) - 1u);
          CHECK(n == prefixes[i - 1u]);
          CHECK(h.starts_with(made, n) && !h.starts_with(n, made));
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
    // Two events against the one of `from`: the continuation lands one
    // event further on.
    const auto base =
        h.append(h.append(History::kRoot, 9u, i + 100000u), 8u, i + 100000u);
    const auto made = h.graft(base, from, tail);
    CHECK(h.depth(made) == length + 2u);
    CHECK(h.ancestor(made, 2u) == base);
    CHECK(h.last(made) == h.last(tail));
  }
  // A long continuation costs a path at its boundary, not a copy of every
  // event for each alternative prefix.
  CHECK(h.size() - before < alternatives * 128u);
}

void short_continuations_do_not_copy_the_long_prefix() {
  using History = cursiv::LlHistory;
  History h;
  auto trunk = History::kRoot;
  constexpr unsigned length = 16384, alternatives = 64, suffix = 6;
  for (unsigned i = 0; i < length; ++i)
    trunk = h.append(trunk, i % 7u, i);
  const auto from = h.append(trunk, 7u, length);
  auto to = from;
  for (unsigned i = 0; i < suffix; ++i)
    to = h.append(to, 8u + i, length + 1u + i, i % 3u);
  std::vector<History::Node> bases;
  for (unsigned i = 0; i < alternatives; ++i)
    bases.push_back(h.append(trunk, 100u + i, length));
  const auto before = h.size();
  for (const auto base : bases) {
    const auto made = h.graft(base, from, to);
    CHECK(h.depth(made) == length + 1u + suffix);
    CHECK(h.ancestor(made, length + 1u) == base);
    auto expected = base;
    for (unsigned i = 0; i < suffix; ++i) {
      const History::Event event{8u + i, length + 1u + i, i % 3u};
      CHECK(h.event(made, length + 1u + i) == event);
      expected = h.append(expected, event.word, event.at, event.lull);
    }
    CHECK(made == expected);
  }
  // These short continuations fit after the queued event of every base.
  // Their cost depends on the suffix, not the length of the shared prefix.
  CHECK(h.size() - before <= alternatives * suffix * 2u);
  CHECK(h.last(History::kRoot) == History::Event{});
}

// Two readings that part only at their last choice, as two calls merged into
// one cell do, hand a long continuation from one to the other by a path. And
// the readings that go on from one full queue share one tree of it.
void siblings_trade_a_long_continuation_by_a_path() {
  using History = cursiv::LlHistory;
  History h;
  constexpr unsigned length = 16384, readings = 64;
  std::vector<History::Node> trunk{History::kRoot};
  for (unsigned i = 0; i < length; ++i)
    trunk.push_back(h.append(trunk.back(), i % 7u, i));
  const auto from = h.append(trunk.back(), 1u, length);
  const auto base = h.append(trunk.back(), 2u, length);
  auto to = from;
  for (unsigned i = 0; i < length; ++i)
    to = h.append(to, i % 5u, length + 1u + i);
  auto before = h.size();
  const auto made = h.graft(base, from, to);
  CHECK(h.size() - before < 32u);
  before = h.size();
  for (unsigned d = length - 8u; d < length; ++d)
    for (unsigned i = 0; i < readings; ++i)
      (void)h.append(trunk[d], 9u + i, d);
  CHECK(h.size() - before < 9u * readings);
  CHECK(h.depth(made) == h.depth(to) && h.common(made, to) == trunk.back());
  CHECK(h.ancestor(made, length + 1u) == base);
  for (const unsigned i : {length + 1u, length + 4096u, 2u * length})
    CHECK(h.event(made, i) == h.event(to, i));
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

void periodic_choices_keep_balanced_paths() {
  using History = cursiv::LlHistory;
  for (const unsigned period : {2u, 7u}) {
    History h;
    std::vector<History::Node> prefixes{History::kRoot};
    constexpr unsigned length = 16384;
    for (unsigned i = 0; i < length; ++i)
      prefixes.push_back(h.append(prefixes.back(), i % period));
    // Lexical histories record ranks without text positions. Repeating ranks
    // must not turn an event's repeated treap priority into a linear spine.
    CHECK(h.size() < length * 16u);
    for (const unsigned cut : {1u, 7u, 8u, 511u, 4096u, length - 1u}) {
      CHECK(h.ancestor(prefixes.back(), cut) == prefixes[cut]);
      CHECK(h.event(prefixes.back(), cut).word == cut % period);
      const auto made = h.graft(prefixes[cut], prefixes[cut], prefixes.back());
      CHECK(made == prefixes.back());
      const auto suffix = h.graft(History::kRoot, prefixes[cut], prefixes.back());
      CHECK(h.graft(prefixes[cut], History::kRoot, suffix) == prefixes.back());
    }
  }
}

void concatenation_keeps_exact_events_across_runs() {
  using History = cursiv::LlHistory;
  using Events = std::vector<History::Event>;
  History h;
  std::mt19937 rng(117);
  std::vector<History::Node> nodes{History::kRoot};
  std::vector<Events> events(1);
  for (unsigned i = 0; i < 2000; ++i) {
    const unsigned base = rng() % nodes.size();
    const unsigned tail = rng() % nodes.size();
    const unsigned cut = rng() % (events[tail].size() + 1u);
    if (events[base].size() + events[tail].size() - cut > 512u) continue;
    const auto from = h.ancestor(nodes[tail], cut);
    const auto made = h.graft(nodes[base], from, nodes[tail]);
    Events expected = events[base];
    expected.insert(expected.end(), events[tail].begin() + cut,
                    events[tail].end());
    auto canonical = History::kRoot;
    for (const auto& e : expected)
      canonical = h.append(canonical, e.word, e.at, e.lull);
    CHECK(made == canonical);
    Events actual;
    h.each(made, 0, [&](const History::Event& e) {
      actual.push_back(e);
      return true;
    });
    CHECK(actual == expected);
    const History::Event next{static_cast<std::uint32_t>(rng() % 3u),
                              static_cast<std::uint32_t>(rng() % 3u),
                              static_cast<std::uint32_t>(rng() % 2u)};
    nodes.push_back(h.append(made, next.word, next.at, next.lull));
    expected.push_back(next);
    events.push_back(std::move(expected));
  }

  // These distinct events deliberately share the event hash. A sequence hash
  // can select a candidate, but neither runs nor histories may trust it alone.
  const History::Event zero{}, collision{0x9E3779B9u, 0x7F4A7C15u, 1u};
  auto a = History::kRoot, b = History::kRoot;
  for (unsigned i = 0; i < 32; ++i) {
    a = h.append(a, zero.word, zero.at, zero.lull);
    b = h.append(b, collision.word, collision.at, collision.lull);
  }
  CHECK(a != b && h.common(a, b) == History::kRoot);
  CHECK(!h.starts_with(a, b) && !h.starts_with(b, a));
  CHECK(h.last(a) == zero && h.last(b) == collision);
}

}  // namespace

void a_repair_invalidates_the_prediction_plan() {
  using cursiv::kLlUnplanned;
  cursiv::LlPlan plan;
  std::vector<cursiv::LlPlanEntry> entries{
      {.at = 4, .decision = 7, .arm = 1},
      {.at = 4, .decision = 7, .arm = 0},
      {.at = 4, .lull = 1, .decision = 7, .arm = 2},
      {.at = 9, .decision = 3, .arm = cursiv::kNoRow},
  };
  const auto laid = plan.install(entries, nullptr, 0);
  CHECK(plan.say(7, 4, 0, 0, false) == 1);
  CHECK(plan.say(7, 4, 0, 0, false) == 0);
  CHECK(plan.say(7, 4, 0, 0, false) == kLlUnplanned);
  CHECK(plan.say(7, 4, 1, 0, false) == 2);
  CHECK(plan.say(7, 5, 0, 0, false) == kLlUnplanned);
  CHECK(plan.say(8, 4, 0, 0, false) == kLlUnplanned);
  CHECK(plan.say(3, 9, 0, 0, false) == cursiv::kNoRow);
  CHECK(plan.say(3, 9, 0, 0, false) == cursiv::kNoRow);
  CHECK(plan.extend(laid, {{.at = 12, .decision = 1, .arm = 1}}));
  CHECK(plan.say(1, 12, 0, 1, false) == kLlUnplanned);
  CHECK(plan.extend(laid, {{.at = 15, .decision = 5, .arm = 3}}));
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
    CHECK(h.append(parent, h.last(at).word) == at);
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
  CHECK(h.last(later).at == 10 && h.last(later).lull == 1);
  CHECK(!h.before(here, later) && !h.before(later, here));
  // A long chain stays logarithmic to climb.
  auto tip = cursiv::LlHistory::kRoot;
  for (unsigned i = 0; i < 200000; ++i) tip = h.append(tip, 1);
  CHECK(h.depth(h.ancestor(tip, 777)) == 777);
  grafts_keep_canonical_prefixes_and_positions();
  long_continuations_share_their_blocks();
  short_continuations_do_not_copy_the_long_prefix();
  siblings_trade_a_long_continuation_by_a_path();
  shared_continuations_keep_the_length_in_range();
  periodic_choices_keep_balanced_paths();
  concatenation_keeps_exact_events_across_runs();
  return cursiv::test::report("LlHistory_test");
}
