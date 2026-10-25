#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/runtime/LrGssDigest.hpp"
#include "cursiv/engines/lr/runtime/LrGssForest.hpp"
#include "cursiv/engines/lr/table/LrColdTables.hpp"
#include "cursiv/test/Check.hpp"

namespace {

using Forest = cursiv::LrGssForest;
using Id = Forest::Id;

void a_late_child_alternative_changes_the_parent_election() {
  Forest forest;
  const Id parent = forest.node();
  const Id child = forest.node();
  const std::array<Id, 1> children{child};
  const std::array<std::uint32_t, 1> zero{0}, one{1}, two{2};
  forest.add(parent, {}, 10, one);
  forest.add(child, {}, 20, two);
  forest.add(parent, children, 30, one);
  forest.add(child, {}, 40, zero);

  CHECK(std::ranges::equal(forest.rank(parent), std::array{0u, 1u}));
  CHECK(forest.steps(parent) == std::vector<Id>({40, 30}));
  CHECK(std::ranges::equal(forest.rank(child), zero));
  CHECK(forest.elect(parent) != Forest::none);
}

void the_first_zero_arm_precedes_a_later_better_choice() {
  Forest forest;
  const Id parent = forest.node();
  const Id first = forest.node();
  const Id later = forest.node();
  const std::array<Id, 1> firstChild{first}, laterChild{later};
  const std::array<std::uint32_t, 1> zero{0}, one{1};
  forest.add(first, {}, 10, zero);
  forest.add(later, {}, 20, one);
  forest.add(parent, laterChild, 30, zero);
  forest.add(parent, firstChild, 40, one);

  CHECK(std::ranges::equal(forest.rank(parent), std::array{0u, 1u}));
  CHECK(forest.steps(parent) == std::vector<Id>({10, 40}));
}

void shared_children_and_equal_ranks_preserve_replay_order() {
  Forest forest;
  const Id parent = forest.node();
  const Id shared = forest.node();
  const Id seed = forest.node();
  const std::array<Id, 3> children{seed, shared, shared};
  const std::array<std::uint32_t, 1> zero{0}, one{1};
  forest.add(seed, {}, Forest::none, {});
  forest.add(shared, {}, 10, zero);
  forest.add(parent, children, 20, one);
  forest.add(parent, children, 20, one);
  forest.add(parent, children, 30, one);

  CHECK(forest.size() == 3);
  CHECK(std::ranges::equal(forest.rank(parent), std::array{0u, 0u, 1u}));
  CHECK(forest.steps(parent) == std::vector<Id>({10, 10, 20}));
  CHECK(forest.steps(seed).empty());
}

void a_cycle_is_refused_before_replay() {
  Forest forest;
  const Id parent = forest.node();
  const Id child = forest.node();
  const std::array<Id, 1> down{child}, up{parent};
  forest.add(parent, down, 10, {});
  forest.add(child, up, 20, {});
  bool refused = false;
  try {
    (void)forest.steps(parent);
  } catch (const std::logic_error&) {
    refused = true;
  }
  CHECK(refused);
}

void clearing_allows_a_different_election_in_the_next_generation() {
  Forest forest;
  for (const unsigned generation : {0u, 1u}) {
    const Id parent = forest.node();
    const Id child = forest.node();
    const std::array<Id, 1> children{child};
    const std::array<std::uint32_t, 1> zero{0}, one{1};
    forest.add(parent, children, 30, zero);
    forest.add(child, {}, 10, generation == 0 ? zero : one);
    forest.add(child, {}, 20, generation == 0 ? one : zero);

    CHECK(forest.steps(parent) ==
          std::vector<Id>({generation == 0 ? 10u : 20u, 30}));
    CHECK(std::ranges::equal(forest.rank(parent), std::array{0u, 0u}));
    forest.clear();
    CHECK(forest.size() == 0 && forest.alternatives() == 0);
    CHECK(forest.links() == 0 && forest.rankCells() == 0);
    CHECK(forest.choiceWords() == 0);
  }
}

void prefixes_empty_ranks_and_late_winners_keep_lexical_order() {
  Forest forest;
  const Id parent = forest.node();
  const std::array<std::uint32_t, 4> initial{1, 8, 7, 6};
  const std::array<std::uint32_t, 8> losing{2, 0, 0, 0, 0, 0, 0, 0};
  const std::array<std::uint32_t, 5> winning{1, 8, 6, 9, 10};
  const std::array<std::uint32_t, 3> prefix{1, 8, 6};
  forest.add(parent, {}, 10, initial);
  forest.add(parent, {}, 20, losing);
  forest.add(parent, {}, 30, initial);
  forest.add(parent, {}, 40, winning);
  forest.add(parent, {}, 50, prefix);
  forest.add(parent, {}, 60, winning);
  CHECK(std::ranges::equal(forest.rank(parent), prefix));
  CHECK(forest.steps(parent) == std::vector<Id>({50}));
  CHECK(forest.rankCells() == 0);

  forest.clear();
  const Id empty = forest.node();
  forest.add(empty, {}, 10, initial);
  forest.add(empty, {}, 20, {});
  forest.add(empty, {}, 30, {});
  forest.add(empty, {}, 40, initial);
  CHECK(forest.rank(empty).empty());
  CHECK(forest.steps(empty) == std::vector<Id>({20}));
}

void add_segmented(Forest& forest, Id parent,
                   const std::vector<std::uint32_t>& words,
                   std::size_t first, std::size_t second, Id step) {
  const std::array<Id, 2> children{forest.node(), forest.node()};
  const std::span<const std::uint32_t> rank(words);
  forest.add(children[0], {}, Forest::none, rank.first(first));
  forest.add(children[1], {}, Forest::none,
             rank.subspan(first, second - first));
  forest.add(parent, children, step, rank.subspan(second));
}

void segment_boundaries_do_not_change_comparison_or_first_ties() {
  std::vector<std::vector<std::uint32_t>> words{{}};
  for (unsigned length = 1; length <= 3; ++length) {
    for (unsigned bits = 0; bits != (1u << length); ++bits) {
      std::vector<std::uint32_t> rank;
      for (unsigned i = 0; i != length; ++i)
        rank.push_back((bits & (1u << i)) == 0 ? 0u : ~0u);
      words.push_back(std::move(rank));
    }
  }
  Forest forest;
  for (const auto& initial : words) {
    for (const auto& later : words) {
      const bool wins = std::lexicographical_compare(
          later.begin(), later.end(), initial.begin(), initial.end());
      for (std::size_t a = 0; a <= initial.size(); ++a) {
        for (std::size_t b = a; b <= initial.size(); ++b) {
          for (std::size_t c = 0; c <= later.size(); ++c) {
            for (std::size_t d = c; d <= later.size(); ++d) {
              const Id parent = forest.node();
              add_segmented(forest, parent, initial, a, b, 10);
              add_segmented(forest, parent, later, c, d, 20);
              CHECK(std::ranges::equal(forest.rank(parent), wins ? later : initial));
              CHECK(forest.steps(parent) == std::vector<Id>({wins ? 20u : 10u}));
              forest.clear();
            }
          }
        }
      }
    }
  }
}

void shared_segments_survive_arena_growth_and_keep_the_first_equal_winner() {
  Forest forest;
  const Id parent = forest.node();
  const Id prefix = forest.node();
  const Id empty = forest.node();
  const Id tail = forest.node();
  const Id laterRoot = forest.node();
  std::vector<std::uint32_t> first(4097), last(2049), extra(32769, ~0u);
  for (std::size_t i = 0; i != first.size(); ++i) first[i] = i % 7;
  for (std::size_t i = 0; i != last.size(); ++i) last[i] = i % 11;
  forest.add(prefix, {}, 101, first);
  forest.add(empty, {}, Forest::none, {});
  forest.add(tail, {}, 102, last);
  const std::array<Id, 4> withEmpty{prefix, empty, tail, prefix};
  const std::array<Id, 3> shared{prefix, tail, prefix};
  const std::array<std::uint32_t, 2> worse{2, ~0u}, better{1, ~0u};
  forest.add(parent, withEmpty, 10, worse);
  forest.add(parent, shared, 20, better);
  forest.add(parent, withEmpty, 30, better);
  const std::array<Id, 1> parentChild{parent};
  forest.add(laterRoot, parentChild, 40, extra);

  auto expected = first;
  expected.insert(expected.end(), last.begin(), last.end());
  expected.insert(expected.end(), first.begin(), first.end());
  expected.insert(expected.end(), better.begin(), better.end());
  CHECK(std::ranges::equal(forest.rank(parent), expected));
  CHECK(forest.steps(parent) == std::vector<Id>({101, 102, 101, 20}));
  const auto earlier = expected;
  expected.insert(expected.end(), extra.begin(), extra.end());
  CHECK(std::ranges::equal(forest.rank(laterRoot), expected));
  CHECK(std::ranges::equal(forest.rank(parent), earlier));
  CHECK(forest.steps(laterRoot) == std::vector<Id>({101, 102, 101, 20, 40}));
}

void block_boundaries_keep_uint32_order_instead_of_byte_order() {
  const std::array<std::size_t, 9> lengths{0, 1, 15, 16, 17, 31, 32, 33, 65};
  const std::array<std::size_t, 12> positions{
      0, 1, 14, 15, 16, 17, 30, 31, 32, 33, 63, 64};
  Forest forest;
  for (const auto leftSize : lengths) {
    for (const auto rightSize : lengths) {
      const std::vector<std::uint32_t> initial(leftSize, 256);
      const auto check = [&](const std::vector<std::uint32_t>& later) {
        const bool wins = std::lexicographical_compare(
            later.begin(), later.end(), initial.begin(), initial.end());
        for (unsigned split = 0; split != 3; ++split) {
          const Id parent = forest.node();
          if (split == 0) {
            forest.add(parent, {}, 10, initial);
            forest.add(parent, {}, 20, later);
          } else if (split == 1) {
            add_segmented(forest, parent, initial,
                           std::min<std::size_t>(16, leftSize),
                           std::min<std::size_t>(32, leftSize), 10);
            add_segmented(forest, parent, later,
                           std::min<std::size_t>(15, rightSize),
                           std::min<std::size_t>(31, rightSize), 20);
          } else {
            add_segmented(forest, parent, initial, 0,
                           std::min<std::size_t>(16, leftSize), 10);
            add_segmented(forest, parent, later,
                           std::min<std::size_t>(17, rightSize), rightSize, 20);
          }
          CHECK(std::ranges::equal(forest.rank(parent), wins ? later : initial));
          CHECK(forest.steps(parent) == std::vector<Id>({wins ? 20u : 10u}));
          forest.clear();
        }
      };
      std::vector<std::uint32_t> later(rightSize, 256);
      check(later);
      for (const auto at : positions) {
        if (at >= rightSize) continue;
        for (const std::uint32_t word : {0u, 255u, ~0u}) {
          later[at] = word;
          check(later);
        }
        later[at] = 256;
      }
    }
  }
}

void deep_rank_prefixes_are_shared_without_copying_their_words() {
  Forest forest;
  constexpr std::size_t count = 32768;
  std::vector<std::uint32_t> expected;
  expected.reserve(count);
  Id left = Forest::none;
  Id right = Forest::none;
  for (std::size_t i = 0; i != count; ++i) {
    expected.push_back(static_cast<std::uint32_t>((i * 257) % 65521));
    const Id next = forest.node();
    const std::array<Id, 1> child{left};
    const std::array<std::uint32_t, 1> word{expected.back()};
    forest.add(next, left == Forest::none ? std::span<const Id>{} : child,
               Forest::none, word);
    left = next;
    // The other spelling has the same words, in different segments.
    if (i % 3 == 2 || i + 1 == count) {
      const Id nextRight = forest.node();
      const std::array<Id, 1> previous{right};
      const auto size = i % 3 + 1;
      forest.add(nextRight,
                 right == Forest::none ? std::span<const Id>{} : previous,
                 Forest::none,
                 std::span<const std::uint32_t>(expected).last(size));
      right = nextRight;
    }
  }
  const std::array<Id, 1> lhs{left}, rhs{right};
  CHECK(!forest.precedes(lhs, rhs));
  CHECK(!forest.precedes(rhs, lhs));
  CHECK(forest.rank(left) == expected);
  CHECK(forest.rank(right) == expected);
  CHECK(forest.choiceWords() == 2 * count);
  CHECK(forest.rankCells() <= 32 * (forest.links() + forest.size()));

  const Id parent = forest.node();
  const std::array<std::uint32_t, 1> worse{256}, better{255};
  forest.add(parent, lhs, 10, worse);
  forest.add(parent, rhs, 20, better);
  forest.add(parent, lhs, 30, better);
  CHECK(forest.steps(parent) == std::vector<Id>({20}));
  expected.push_back(255);
  CHECK(forest.rank(parent) == expected);
  CHECK(forest.choiceWords() == 2 * count + 3);
}

void an_exponentially_shared_prefix_is_compared_without_spelling_it_out() {
  Forest forest;
  Id prefix = forest.node();
  const std::array<std::uint32_t, 1> word{~0u};
  forest.add(prefix, {}, Forest::none, word);
  for (unsigned level = 0; level != 40; ++level) {
    const Id next = forest.node();
    const std::array<Id, 2> children{prefix, prefix};
    forest.add(next, children, Forest::none, {});
    prefix = next;
  }
  const Id twice = forest.node();
  const std::array<Id, 2> separate{prefix, prefix};
  forest.add(twice, separate, Forest::none, {});

  const Id left = forest.node(), right = forest.node();
  const std::array<Id, 1> joined{twice};
  const std::array<std::uint32_t, 1> better{0}, worse{1};
  forest.add(left, separate, Forest::none, better);
  forest.add(right, joined, Forest::none, worse);
  const std::array<Id, 1> lhs{left}, rhs{right};
  CHECK(forest.precedes(lhs, rhs));
  CHECK(!forest.precedes(rhs, lhs));
  CHECK(forest.choiceWords() == 3);
  CHECK(forest.rankCells() <= 32 * (forest.links() + forest.size()));
}

void a_virtual_rank_cannot_wrap_its_length() {
  Forest forest;
  const Id leaf = forest.node();
  forest.add(leaf, {}, Forest::none, std::array{1u});
  Id prefix = leaf;
  for (unsigned level = 0; level != 63; ++level) {
    const Id next = forest.node();
    forest.add(next, std::array{prefix, prefix}, Forest::none, {});
    prefix = next;
  }
  // A 2^63-word rank is valid and is compared without materializing it.
  const std::array large{prefix}, small{leaf};
  CHECK(forest.order(large, small) == Forest::kOpen);
  const Id overflow = forest.node();
  forest.add(overflow, std::array{prefix, prefix}, Forest::none, {});
  CHECK(forest.size() == 65);
  const std::array tooLarge{overflow};
  bool refused = false;
  try {
    (void)forest.order(tooLarge, small);
  } catch (const std::length_error&) {
    refused = true;
  }
  CHECK(refused);
  CHECK(forest.order(large, small) == Forest::kOpen);
  forest.clear();
  const Id fresh = forest.node();
  forest.add(fresh, {}, Forest::none, std::array{2u});
  CHECK(forest.rank(fresh) == std::vector<std::uint32_t>{2u});
}

void a_frame_one_stack_reads_whole_leaves_the_stacks_open() {
  Forest forest;
  const auto leaf = [&](std::span<const std::uint32_t> words) {
    const Id n = forest.node();
    forest.add(n, {}, Forest::none, words);
    return n;
  };
  const std::array<std::uint32_t, 1> zero{0}, one{1}, five{5};
  const std::array<std::uint32_t, 2> zeros{0, 0}, zeroOne{0, 1}, zeroTwo{0, 2};
  const Id shorter = leaf(zero), longer = leaf(zeros);
  const Id late = leaf(five), soon = leaf(one);
  const std::array<Id, 2> lhs{shorter, late}, rhs{longer, soon};
  CHECK(forest.order(lhs, rhs) == Forest::kOpen);
  CHECK(forest.order(rhs, lhs) == Forest::kOpen);
  CHECK(!forest.precedes(lhs, rhs) && !forest.precedes(rhs, lhs));

  const Id first = leaf(zeroOne), second = leaf(zeroTwo);
  const std::array<Id, 2> parted{first, late}, other{second, soon};
  CHECK(forest.order(parted, other) < 0 && forest.order(other, parted) > 0);
  const std::array<Id, 2> shared{first, soon};
  CHECK(forest.order(parted, shared) > 0);
}

// The lexer's rank of the first token a frame reads comes before the words
// written in front of it, for the election of a node and the order of two
// stacks alike.
void a_frame_is_ordered_by_the_lexers_rank_of_its_first_token() {
  Forest forest;
  const auto token = [&](std::uint32_t rank) {
    const Id n = forest.node();
    const std::array<std::uint32_t, 1> word{rank};
    forest.add(n, {}, Forest::none, word, {}, Forest::Kind::ranked);
    return n;
  };
  const auto arm = [&](Id kid, std::uint32_t written) {
    const Id n = forest.node();
    const std::array<Id, 1> kids{kid};
    const std::array<std::uint32_t, 1> word{written};
    const std::array<std::uint16_t, 1> before{0};
    forest.add(n, kids, Forest::none, word, before);
    return n;
  };
  const Id late = arm(token(5), 0), soon = arm(token(2), 1);
  const std::array<Id, 1> left{late}, right{soon};
  CHECK(forest.order(left, right) > 0 && forest.order(right, left) < 0);
  CHECK(forest.precedes(right, left) && !forest.precedes(left, right));
  const Id alike = arm(token(5), 1);
  const std::array<Id, 1> same{alike};
  CHECK(forest.precedes(left, same) && !forest.precedes(same, left));

  const Id parent = forest.node();
  const std::array<Id, 1> first{late}, second{soon};
  forest.add(parent, first, 10, std::array<std::uint32_t, 0>{});
  forest.add(parent, second, 20, std::array<std::uint32_t, 0>{});
  CHECK(forest.steps(parent).size() == 1 && forest.steps(parent)[0] == 20);
}

// Random packed forests, each node over earlier ones: the election, the words
// it reads and the steps it keeps must be those of spelling the ranks out.
void random_forests_elect_what_spelled_ranks_elect() {
  std::uint64_t seed = 0x243F6A8885A308D3ull;
  const auto next = [&](std::uint64_t bound) {
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return bound == 0 ? 0 : seed % bound;
  };
  for (int round = 0; round != 300; ++round) {
    Forest forest;
    const std::size_t count = 2 + next(40);
    std::vector<std::vector<std::uint32_t>> words;
    std::vector<std::vector<Id>> steps;
    Id step = 0;
    for (std::size_t i = 0; i != count; ++i) {
      const Id node = forest.node();
      CHECK(node == i);
      std::vector<std::uint32_t> best;
      std::vector<Id> bestSteps;
      const std::size_t alternatives = 1 + next(4);
      for (std::size_t a = 0; a != alternatives; ++a) {
        std::vector<Id> children;
        std::vector<std::uint32_t> spelled;
        std::vector<Id> spelledSteps;
        for (std::size_t c = next(4); c-- > 0 && i != 0;) {
          const auto child = static_cast<Id>(i - 1 - next(std::min<std::size_t>(i, 6)));
          if (spelled.size() + words[child].size() > 1500) continue;
          children.push_back(child);
          spelled.insert(spelled.end(), words[child].begin(), words[child].end());
          spelledSteps.insert(spelledSteps.end(), steps[child].begin(),
                              steps[child].end());
        }
        std::vector<std::uint32_t> choices(next(4) == 0 ? 20 + next(80) : next(3));
        for (auto& word : choices) word = static_cast<std::uint32_t>(next(3));
        spelled.insert(spelled.end(), choices.begin(), choices.end());
        spelledSteps.push_back(step);
        forest.add(node, children, step++, choices);
        if (a == 0 || std::ranges::lexicographical_compare(spelled, best)) {
          best = std::move(spelled);
          bestSteps = std::move(spelledSteps);
        }
      }
      words.push_back(std::move(best));
      steps.push_back(std::move(bestSteps));
    }
    for (std::size_t i = 0; i != count; ++i) {
      CHECK(forest.rank(static_cast<Id>(i)) == words[i]);
      CHECK(forest.steps(static_cast<Id>(i)) == steps[i]);
    }
    // Two stacks part inside a frame, or stay open where one frame is a
    // prefix of the other.
    for (int pair = 0; pair != 20; ++pair) {
      std::vector<Id> left, right;
      for (std::size_t k = next(3); k-- > 0;) {
        left.push_back(static_cast<Id>(next(count)));
        right.push_back(next(3) == 0 ? left.back()
                                     : static_cast<Id>(next(count)));
      }
      if (next(5) == 0) right.push_back(static_cast<Id>(next(count)));
      int want = left.size() == right.size() ? 0 : Forest::kOpen;
      for (std::size_t k = 0; want == 0 && k != left.size(); ++k) {
        const auto& a = words[left[k]];
        const auto& b = words[right[k]];
        const auto [x, y] = std::ranges::mismatch(a, b);
        if (x != a.end() && y != b.end())
          want = *x < *y ? -1 : 1;
        else if (a.size() != b.size())
          want = Forest::kOpen;
      }
      CHECK(forest.order(left, right) == want);
      CHECK(forest.precedes(left, right) == (want < 0));
    }
  }
}

}  // namespace

namespace gss_digest {
namespace {

using cursiv::LrGssDigest;
using cursiv::LrGssForest;
using Id = LrGssForest::Id;

// Every step is taken at position 5; those below 5 are the only step of their
// vertex.
LrGssDigest::Facts facts(Id step) { return {5, step >= 5}; }

std::vector<Id> forks(const LrGssDigest& digest, LrGssDigest::Rope::Id rope) {
  std::vector<Id> out;
  LrGssDigest::Rope::Cursor c;
  for (c.seek(digest.forks(), rope, 0); !c.done(); c.next())
    out.push_back(c.value());
  return out;
}

void the_fork_steps_come_in_the_order_the_reading_took_them() {
  LrGssForest forest;
  LrGssDigest digest;
  const Id parent = forest.node(), left = forest.node(), right = forest.node();
  const std::array<Id, 2> kids{left, right};
  forest.add(left, {}, 7, {});
  forest.add(right, {}, 2, {});
  forest.add(parent, kids, 9, {});
  const auto rope = digest.of(forest, parent, facts);
  CHECK(forks(digest, rope) == std::vector<Id>({7, 9}));
  CHECK(digest.forks().length(rope) == 2);
}

void a_step_that_forked_nothing_is_left_out() {
  LrGssForest forest;
  LrGssDigest digest;
  const Id node = forest.node();
  forest.add(node, {}, 3, {});
  CHECK(digest.of(forest, node, facts) == LrGssDigest::Rope::none);
}

// Every fork step a derivation took at the position it ends on is read, however
// many: two stacks are told apart by the last of them.
void a_derivation_ending_on_many_forks_keeps_every_one() {
  constexpr Id kSteps = 40;
  LrGssForest forest;
  LrGssDigest digest;
  Id below = LrGssForest::none;
  for (Id step = 6; step != kSteps + 6; ++step) {
    const Id node = forest.node();
    const std::array<Id, 1> kid{below};
    forest.add(node, below == LrGssForest::none ? std::span<const Id>{} : kid,
               step, {});
    below = node;
  }
  const auto rope = digest.of(forest, below, facts);
  CHECK(digest.forks().length(rope) == kSteps);
  const auto cut = digest.forks().first_where(
      rope, [](const cursiv::LrGssForkAgg& a) { return a.at >= 5; });
  CHECK(cut == 0);
  CHECK(forks(digest, rope).size() == kSteps);
}

}  // namespace

void run() {
  the_fork_steps_come_in_the_order_the_reading_took_them();
  a_step_that_forked_nothing_is_left_out();
  a_derivation_ending_on_many_forks_keeps_every_one();
}
}  // namespace gss_digest

int main() {
  gss_digest::run();

  a_late_child_alternative_changes_the_parent_election();
  the_first_zero_arm_precedes_a_later_better_choice();
  shared_children_and_equal_ranks_preserve_replay_order();
  a_cycle_is_refused_before_replay();
  clearing_allows_a_different_election_in_the_next_generation();
  prefixes_empty_ranks_and_late_winners_keep_lexical_order();
  segment_boundaries_do_not_change_comparison_or_first_ties();
  shared_segments_survive_arena_growth_and_keep_the_first_equal_winner();
  block_boundaries_keep_uint32_order_instead_of_byte_order();
  deep_rank_prefixes_are_shared_without_copying_their_words();
  an_exponentially_shared_prefix_is_compared_without_spelling_it_out();
  a_virtual_rank_cannot_wrap_its_length();
  a_frame_one_stack_reads_whole_leaves_the_stacks_open();
  a_frame_is_ordered_by_the_lexers_rank_of_its_first_token();
  random_forests_elect_what_spelled_ranks_elect();
  return cursiv::test::report("LrGssForest_test");
}
