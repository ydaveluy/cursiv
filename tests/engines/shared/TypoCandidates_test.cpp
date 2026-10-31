#include "cursiv/engines/shared/runtime/GestureRank.hpp"
#include "cursiv/engines/shared/runtime/TiedInserts.hpp"
#include "cursiv/engines/shared/runtime/TypoCandidates.hpp"
#include "cursiv/test/Check.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace typo_candidates_cases {

// The re-readings a walk emits are kept once each at their least cost and
// ranked as an ordered insertion ranks them, and a token that ends at every
// byte of a long line costs its number of candidates.

namespace {

using cursiv::Typo;

// One insertion at a time into a ranked list: what the collector replaced.
std::vector<Typo> by_insertion(const std::vector<Typo>& emitted) {
  std::vector<Typo> out;
  const auto before = [](const Typo& a, const Typo& b) {
    return a.cost != b.cost ? a.cost < b.cost : a.end > b.end;
  };
  for (const Typo& candidate : emitted) {
    const auto same = std::find_if(out.begin(), out.end(), [&](const Typo& o) {
      return o.term == candidate.term && o.end == candidate.end;
    });
    if (same != out.end()) {
      if (same->cost <= candidate.cost) continue;
      out.erase(same);
    }
    auto last = std::lower_bound(out.begin(), out.end(), candidate, before);
    while (last != out.end() && last->cost == candidate.cost &&
           last->end == candidate.end)
      ++last;
    out.insert(last, candidate);
  }
  return out;
}

Typo typo(std::uint32_t end, std::uint16_t term, std::uint32_t cost) {
  Typo t;
  t.end = end;
  t.term = term;
  t.cost = cost;
  return t;
}

void ranks_as_an_insertion_does() {
  std::mt19937 rng(20260929);
  for (unsigned round = 0; round < 400; ++round) {
    std::vector<Typo> emitted;
    const unsigned n = 1 + rng() % (round % 4 == 0 ? 400 : 40);
    for (unsigned i = 0; i < n; ++i)
      emitted.push_back(
          typo(rng() % 30, static_cast<std::uint16_t>(rng() % 4), rng() % 4));
    cursiv::TypoCandidates c;
    for (const Typo& t : emitted) c.add(t);
    const std::vector<Typo> got = c.ranked();
    const std::vector<Typo> want = by_insertion(emitted);
    CHECK(got.size() == want.size());
    for (std::size_t k = 0; k < got.size() && k < want.size(); ++k)
      CHECK(got[k].end == want[k].end && got[k].term == want[k].term &&
            got[k].cost == want[k].cost);
  }
}

void a_long_line_costs_its_candidates() {
  const auto start = std::chrono::steady_clock::now();
  cursiv::TypoCandidates c;
  constexpr std::uint32_t kEnds = 400000;
  for (std::uint32_t end = 1; end <= kEnds; ++end) c.add(typo(end, 15, 1));
  for (std::uint32_t end = 1; end <= kEnds; ++end) c.add(typo(end, 15, 2));
  const std::vector<Typo> got = c.ranked();
  CHECK(got.size() == kEnds && got.front().end == kEnds && got.back().end == 1);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
}

}  // namespace

void run() {
  cursiv::test::count_expectations();
  ranks_as_an_insertion_does();
  a_long_line_costs_its_candidates();
}

}  // namespace typo_candidates_cases

namespace gesture_rank_cases {

// Ranking two paths costs the distance to the link they share: paths of two
// million links that part at their ends rank at once, and small forests rank
// as the depths say.

namespace {

using cursiv::kNoGestureLink;

struct Link {
  std::uint32_t parent = kNoGestureLink;
  std::uint32_t choice = 0;
};

// The order two paths rank in, by their depths: the lower choice where they
// part, a prefix first.
bool ranks_by_depth(const std::vector<Link>& links, std::uint32_t a,
                    std::uint32_t b) {
  if (a == b) return false;
  const auto depth = [&](std::uint32_t x) {
    std::uint32_t d = 0;
    for (; x != kNoGestureLink; x = links[x].parent) ++d;
    return d;
  };
  std::uint32_t da = depth(a), db = depth(b);
  const bool shorter = da < db;
  for (; da > db; --da) a = links[a].parent;
  for (; db > da; --db) b = links[b].parent;
  if (a == b) return shorter;
  while (links[a].parent != links[b].parent) {
    a = links[a].parent;
    b = links[b].parent;
  }
  return links[a].choice != links[b].choice ? links[a].choice < links[b].choice
                                            : a < b;
}

void small_forests_rank_as_the_depths_say() {
  std::mt19937 rng(929);
  for (unsigned round = 0; round < 300; ++round) {
    cursiv::GestureRank rank;
    std::vector<Link> links;
    const std::uint32_t n = 2 + rng() % 40;
    for (std::uint32_t i = 0; i < n; ++i) {
      const bool root = i == 0 || rng() % 7 == 0;
      links.push_back(
          Link{root ? kNoGestureLink : static_cast<std::uint32_t>(rng() % i),
               static_cast<std::uint32_t>(rng() % 3)});
    }
    for (std::uint32_t a = 0; a < n; ++a)
      for (std::uint32_t b = 0; b < n; ++b)
        CHECK(rank.before(links, a, b) == ranks_by_depth(links, a, b));
  }
}

void long_paths_rank_at_their_divergence() {
  constexpr std::uint32_t kLong = 2'000'000;
  cursiv::GestureRank rank;
  std::vector<Link> links;
  for (std::uint32_t i = 0; i < kLong; ++i)
    links.push_back(Link{i == 0 ? kNoGestureLink : i - 1, 0});
  links.push_back(Link{kLong - 1, 1});
  links.push_back(Link{kLong - 1, 2});
  for (unsigned k = 0; k < 100000; ++k) {
    CHECK(rank.before(links, kLong, kLong + 1));
    CHECK(!rank.before(links, kLong + 1, kLong));
  }
}

}  // namespace

void run() {
  small_forests_rank_as_the_depths_say();
  long_paths_rank_at_their_divergence();
}

}  // namespace gesture_rank_cases

namespace tied_inserts_cases {

// What a dropped path first inserted stays named under the path that kept its
// future, and moves with that path when it takes the dropped one's links.

namespace {

using cursiv::kNoGestureLink;
using cursiv::TiedInserts;

struct Link {
  std::uint32_t parent = kNoGestureLink;
  std::uint32_t choice = 0;
  std::uint32_t kind = TiedInserts::kArm;
};

std::vector<cursiv::ElementId> named(TiedInserts& tied,
                                     const std::vector<Link>& links,
                                     std::uint32_t link) {
  std::vector<cursiv::ElementId> out;
  tied.open();
  tied.name(links, link, [](const Link& l) { return l.kind; }, out);
  TiedInserts::order(out);
  return out;
}

// A path that dies on the kept path's own link leaves its insertion there;
// when the kept path then takes a better-ranked path through the links both
// share, that insertion is still named.
void a_handed_path_keeps_what_its_own_links_held() {
  std::vector<Link> links = {
      {kNoGestureLink, 0, TiedInserts::kArm},  // the root
      {0, 0, 10},                              // an insertion both share
      {1, 0, TiedInserts::kArm},               // where the two part
      {2, 1, TiedInserts::kArm},               // the kept path's own link
      {3, 0, TiedInserts::kArm},               // the kept path's end
      {2, 0, TiedInserts::kArm},               // the better-ranked path's end
      {0, 1, 20},                              // a third path's insertion
      {6, 0, TiedInserts::kArm},               // its end
  };
  const auto kind = [](const Link& l) { return l.kind; };
  cursiv::GestureRank rank;
  TiedInserts tied;
  tied.tie(links, 3, 7, rank.part(links, 7, 3), false, kind);
  CHECK((named(tied, links, 4) == std::vector<cursiv::ElementId>{10, 20}));
  const cursiv::GestureRank::Parting p = rank.part(links, 5, 4);
  const bool handed = cursiv::GestureRank::before(links, p);
  CHECK(handed);
  tied.tie(links, 4, 5, p, handed, kind);
  if (handed) links[4] = links[5];
  CHECK((named(tied, links, 4) == std::vector<cursiv::ElementId>{10, 20}));
}

}  // namespace

void run() { a_handed_path_keeps_what_its_own_links_held(); }

}  // namespace tied_inserts_cases

int main() {
  cursiv::test::count_expectations();
  typo_candidates_cases::run();
  gesture_rank_cases::run();
  tied_inserts_cases::run();
  return cursiv::test::report("TypoCandidates_test");
}
