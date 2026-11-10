#include "cursiv/shared/runtime/TypoCandidates.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/PathRank.hpp"
#include "cursiv/shared/runtime/RepairSearch.hpp"
#include "cursiv/shared/runtime/TiedInserts.hpp"
#include "cursiv/shared/table/FlatMap.hpp"

namespace typo_candidates_cases {

// The re-readings a walk emits are kept once each at their least cost and
// ranked as an ordered insertion ranks them, and a token that ends at every
// byte of a long line costs its number of candidates.

namespace {

using cursiv::engine::Typo;

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
    cursiv::engine::TypoCandidates c;
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
  cursiv::engine::TypoCandidates c;
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

namespace typo_walk_cases {

// A walk keeps one point of each place only where it offers all another one
// would, as cheaply: a point that spelt more bytes is not hidden behind one
// that spelt fewer.

namespace {

using cursiv::TextOffset;
using cursiv::engine::kDeadState;
using cursiv::engine::SymbolId;

// One terminal: letters, then after an underscore letters and digits.
struct Words : cursiv::engine::TypoBounds {
  static constexpr std::uint16_t kLetters = 1, kTail = 2;
  std::span<const std::uint16_t> successors(std::uint16_t s) const {
    static constexpr std::uint16_t start[] = {kLetters};
    static constexpr std::uint16_t letters[] = {kLetters, kTail};
    static constexpr std::uint16_t tail[] = {kTail};
    if (s == 0) return start;
    return s == kLetters ? std::span<const std::uint16_t>(letters)
                         : std::span<const std::uint16_t>(tail);
  }
  std::uint16_t next_byte(std::uint16_t s, unsigned char b) const {
    ++*read;
    const bool letter = b >= 'a' && b <= 'z';
    const bool digit = b >= '0' && b <= '9';
    if (s == 0) return letter ? kLetters : kDeadState;
    if (s == kLetters) return letter ? kLetters : b == '_' ? kTail : kDeadState;
    return letter || digit ? kTail : kDeadState;
  }
  bool original(std::uint16_t, TextOffset) const { return true; }
  template <class Emit>
  void accepted(std::uint16_t s, TextOffset, Emit emit) const {
    if (s != 0) emit(SymbolId{0});
  }
  void refused() const {}
};

// "ab1cd!xyz" read as "ab_1cd", a letter for "!", then "xyz": two faults
// over ten bytes. Skipping the "1" reaches the same place first, with fewer
// bytes spelt and a dearer price.
void a_point_that_spelt_more_is_not_hidden() {
  const std::string_view text = "ab1cd!xyz";
  const auto len = static_cast<TextOffset>(text.size());
  std::uint64_t read = 0;
  const Words words{
      {.src = text.data(), .extent = len, .docLen = len, .read = &read}};
  cursiv::engine::TypoCandidates found;
  cursiv::engine::typo_each(
      words, 0, 0, len, [&](const cursiv::engine::Typo& t) { found.add(t); });
  bool whole = false;
  for (const cursiv::engine::Typo& t : found.ranked())
    if (t.end == len) {
      whole = true;
      CHECK(t.cost == cursiv::engine::repair_typo_cost(2, 10));
    }
  CHECK(whole);
}

}  // namespace

void run() { a_point_that_spelt_more_is_not_hidden(); }

}  // namespace typo_walk_cases

namespace path_rank_cases {

// Ranking two paths costs the distance to the link they share: paths of two
// million links that part at their ends rank at once, and small forests rank
// as the depths say.

namespace {

using cursiv::engine::kNoEditLink;

struct Link {
  std::uint32_t parent = kNoEditLink;
  std::uint32_t choice = 0;
};

// The order two paths rank in, by their depths: the lower choice where they
// part, a prefix first.
bool ranks_by_depth(const std::vector<Link>& links, std::uint32_t a,
                    std::uint32_t b) {
  if (a == b) return false;
  const auto depth = [&](std::uint32_t x) {
    std::uint32_t d = 0;
    for (; x != kNoEditLink; x = links[x].parent) ++d;
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
    cursiv::engine::PathRank rank;
    std::vector<Link> links;
    const std::uint32_t n = 2 + rng() % 40;
    for (std::uint32_t i = 0; i < n; ++i) {
      const bool root = i == 0 || rng() % 7 == 0;
      links.push_back(
          Link{root ? kNoEditLink : static_cast<std::uint32_t>(rng() % i),
               static_cast<std::uint32_t>(rng() % 3)});
    }
    for (std::uint32_t a = 0; a < n; ++a)
      for (std::uint32_t b = 0; b < n; ++b)
        CHECK(rank.before(links, a, b) == ranks_by_depth(links, a, b));
  }
}

void long_paths_rank_at_their_divergence() {
  constexpr std::uint32_t kLong = 2'000'000;
  cursiv::engine::PathRank rank;
  std::vector<Link> links;
  for (std::uint32_t i = 0; i < kLong; ++i)
    links.push_back(Link{i == 0 ? kNoEditLink : i - 1, 0});
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

}  // namespace path_rank_cases

namespace tied_inserts_cases {

// What a dropped path first inserted stays named under the path that kept its
// future, and moves with that path when it takes the dropped one's links.

namespace {

using cursiv::engine::kNoEditLink;
using cursiv::engine::TiedInserts;

struct Link {
  std::uint32_t parent = kNoEditLink;
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
      {kNoEditLink, 0, TiedInserts::kArm},  // the root
      {0, 0, 10},                           // an insertion both share
      {1, 0, TiedInserts::kArm},            // where the two part
      {2, 1, TiedInserts::kArm},            // the kept path's own link
      {3, 0, TiedInserts::kArm},            // the kept path's end
      {2, 0, TiedInserts::kArm},            // the better-ranked path's end
      {0, 1, 20},                           // a third path's insertion
      {6, 0, TiedInserts::kArm},            // its end
  };
  const auto kind = [](const Link& l) { return l.kind; };
  cursiv::engine::PathRank rank;
  TiedInserts tied;
  tied.tie(links, 3, 7, rank.part(links, 7, 3), false, kind);
  CHECK((named(tied, links, 4) == std::vector<cursiv::ElementId>{10, 20}));
  const cursiv::engine::PathRank::Parting p = rank.part(links, 5, 4);
  const bool handed = cursiv::engine::PathRank::before(links, p);
  CHECK(handed);
  tied.tie(links, 4, 5, p, handed, kind);
  if (handed) links[4] = links[5];
  CHECK((named(tied, links, 4) == std::vector<cursiv::ElementId>{10, 20}));
}

}  // namespace

void run() { a_handed_path_keeps_what_its_own_links_held(); }

}  // namespace tied_inserts_cases

namespace repair_elect_cases {

// An election walks every goal's path: it pays the links it walks and reads
// the stop as it walks them, one group of goals or several. A lone goal wins
// as it stands.

namespace {

using cursiv::engine::kNoEditLink;
using cursiv::engine::TiedInserts;

struct Edit {
  std::uint32_t parent = kNoEditLink;
  std::uint32_t choice = 0;
  std::uint32_t kind = TiedInserts::kArm;
};

struct Reading {
  std::uint32_t cost = 0, lost = 0, edits = 0, faults = 0, invented = 0;
  std::uint32_t link = 0;
  bool gone = false;
};

std::uint64_t depth(const std::vector<Edit>& links, std::uint32_t l) {
  std::uint64_t d = 0;
  for (; links[l].parent != kNoEditLink; l = links[l].parent) ++d;
  return d;
}
bool same_depth(const std::vector<Edit>& links, std::uint32_t a,
                std::uint32_t b) {
  return depth(links, a) == depth(links, b);
}
std::uint32_t kind_of(const Edit& g) { return g.kind; }

constexpr std::uint32_t kDeep = 200000;

// Two goals at the end of one long path, and two more on paths of their own.
cursiv::engine::RepairSearch<Edit> search(std::vector<Reading>& rs) {
  cursiv::engine::RepairSearch<Edit> s(1000, {});
  for (std::uint32_t i = 0; i < kDeep; ++i)
    s.links.push_back(Edit{i == 0 ? kNoEditLink : i - 1, 0});
  s.links.push_back(Edit{kDeep - 1, 1});
  s.links.push_back(Edit{kDeep - 1, 2});
  rs = {Reading{.link = kDeep - 1}, Reading{.link = kDeep - 1},
        Reading{.link = kDeep}, Reading{.link = kDeep + 1}};
  s.goals = {0, 1, 2, 3};
  return s;
}

void an_election_reads_the_stop_and_counts_its_walks() {
  const auto race = [](const auto&, auto& reach) {
    std::ranges::fill(reach, cursiv::engine::kRepairReachAll);
  };
  std::vector<Reading> rs;
  {
    auto s = search(rs);
    const cursiv::engine::CancellationPoll free;
    const std::uint32_t won =
        cursiv::engine::repair_elect<depth, same_depth, kind_of>(s, rs, true,
                                                                 free, race);
    CHECK(won == 0);
    CHECK(s.work >= 4 * std::uint64_t{kDeep - 1});
  }
  for (const bool apart : {false, true}) {
    auto s = search(rs);
    if (!apart) s.goals = {0, 1};
    cursiv::CancellationTokenSource source;
    source.request_stop();
    const cursiv::CancellationToken token = source.get_token();
    const cursiv::engine::CancellationPoll stop(token);
    bool threw = false;
    try {
      (void)cursiv::engine::repair_elect<depth, same_depth, kind_of>(
          s, rs, true, stop, race);
    } catch (const cursiv::OperationCancelled&) {
      threw = true;
    }
    CHECK(threw);
    CHECK(s.work <= cursiv::engine::CancellationPoll::kTurns);
  }
}

// The goal's path: an insertion near the root, and below where a dropped path
// parted from it, the insertion that path made first.
cursiv::engine::RepairSearch<Edit> tied_search() {
  cursiv::engine::RepairSearch<Edit> s(1000, {});
  s.links = {
      {kNoEditLink, 0, TiedInserts::kArm},  // the root
      {0, 0, 10},                           // an insertion
      {1, 0, TiedInserts::kArm},            // where the paths part
      {2, 0, TiedInserts::kArm},            // the goal's end
      {0, 1, 20},                           // the dropped path's insertion
      {4, 0, TiedInserts::kArm},            // its end
  };
  cursiv::engine::PathRank rank;
  s.tied.tie(s.links, 3, 5, rank.part(s.links, 5, 3), false, kind_of);
  return s;
}

// A lone goal wins with no race, and names what ties with it as it would
// beside a goal that makes the same edits.
void a_lone_goal_wins_as_it_stands() {
  const auto no_race = [](const auto&, auto&) { CHECK(false); };
  const std::vector<Reading> rs = {Reading{.link = 3}, Reading{.link = 3}};
  for (const bool roots : {false, true}) {
    const cursiv::engine::CancellationPoll free;
    auto lone = tied_search();
    lone.goals = {1};
    CHECK((cursiv::engine::repair_elect<depth, same_depth, kind_of>(
               lone, rs, roots, free, no_race) == 1));
    CHECK((lone.also == std::vector<cursiv::ElementId>{10, 20}));
    CHECK(lone.work == 3);
    auto both = tied_search();
    both.goals = {0, 1};
    CHECK((cursiv::engine::repair_elect<depth, same_depth, kind_of>(
               both, rs, roots, free, no_race) == 0));
    CHECK(both.also == lone.also);
  }
}

// A stop asked for while the goals race is read before another path is
// walked, however short the paths; a lone goal reads it before its walk.
void a_stop_is_read_before_each_walk() {
  const std::vector<Reading> rs = {Reading{.link = 1}, Reading{.link = 2},
                                   Reading{.link = 3}};
  for (const bool lone : {false, true}) {
    cursiv::engine::RepairSearch<Edit> s(1000, {});
    s.links = {{kNoEditLink}, {0}, {1}, {2}};
    s.goals = lone ? std::vector<std::uint32_t>{2}
                   : std::vector<std::uint32_t>{0, 1, 2};
    cursiv::CancellationTokenSource source;
    if (lone) source.request_stop();
    const cursiv::CancellationToken token = source.get_token();
    const cursiv::engine::CancellationPoll stop(token);
    bool raced = false;
    const auto race = [&](const auto&, auto& reach) {
      raced = true;
      std::ranges::fill(reach, cursiv::engine::kRepairReachAll);
      source.request_stop();
    };
    bool threw = false;
    try {
      (void)cursiv::engine::repair_elect<depth, same_depth, kind_of>(
          s, rs, false, stop, race);
    } catch (const cursiv::OperationCancelled&) {
      threw = true;
    }
    CHECK(threw);
    CHECK(raced == !lone);
    CHECK(s.also.empty());
  }
}

}  // namespace

}  // namespace repair_elect_cases

namespace flat_map_cases {

// A key asked for again is found where it stands: the map grows for a key it
// makes, never for one it holds, so what it handed out stays put.
void a_key_held_is_found_in_place() {
  cursiv::engine::FlatMap<std::uint32_t> map;
  for (std::uint64_t key = 0; key < 4096; ++key) {
    CHECK(map.emplace(key).second);
    const std::uint32_t* held = map.find(0);
    const auto again = map.emplace(0);
    CHECK(!again.second && &again.first == held);
    if (&again.first != held) return;
  }
}

}  // namespace flat_map_cases

int main() {
  cursiv::test::count_expectations();
  typo_candidates_cases::run();
  typo_walk_cases::run();
  path_rank_cases::run();
  tied_inserts_cases::run();
  repair_elect_cases::an_election_reads_the_stop_and_counts_its_walks();
  repair_elect_cases::a_lone_goal_wins_as_it_stands();
  repair_elect_cases::a_stop_is_read_before_each_walk();
  flat_map_cases::a_key_held_is_found_in_place();
  return cursiv::test::report("TypoCandidates_test");
}
