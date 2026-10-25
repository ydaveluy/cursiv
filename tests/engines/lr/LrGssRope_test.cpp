#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cursiv/engines/lr/runtime/LrGssIndex.hpp"
#include "cursiv/engines/lr/runtime/LrGssPool.hpp"
#include "cursiv/engines/lr/runtime/LrGssRope.hpp"
#include "cursiv/engines/lr/runtime/LrGssRuns.hpp"
#include "cursiv/test/Check.hpp"

namespace {

using cursiv::LrGssForkAgg;
using cursiv::LrGssNoAgg;
using Spell = cursiv::LrGssRope<LrGssNoAgg>;
using Forks = cursiv::LrGssRope<LrGssForkAgg>;

Spell::Id chunk(Spell& rope, const std::vector<std::uint32_t>& values) {
  Spell::Id r = Spell::none;
  for (const std::uint32_t v : values) r = rope.concat(r, rope.leaf(v));
  return r;
}

bool balanced(const Spell& rope, std::size_t size, Spell::Id r) {
  const double bound = 1.45 * std::log2(static_cast<double>(size) + 2) + 2;
  return rope.depth(r) <= bound;
}

void a_rope_reads_what_it_was_joined_from() {
  std::mt19937 rng(7);
  Spell rope;
  std::vector<std::vector<std::uint32_t>> held;
  std::vector<Spell::Id> ids;
  for (unsigned round = 0; round != 300; ++round) {
    std::vector<std::uint32_t> values;
    Spell::Id id = Spell::none;
    if (held.empty() || rng() % 4 == 0) {
      const unsigned n = rng() % 9;
      for (unsigned i = 0; i != n; ++i) values.push_back(rng() % 5);
      id = chunk(rope, values);
    } else {
      const unsigned pieces = 1 + rng() % 4;
      for (unsigned i = 0; i != pieces; ++i) {
        const std::size_t k = rng() % held.size();
        values.insert(values.end(), held[k].begin(), held[k].end());
        id = rope.concat(id, ids[k]);
      }
    }
    CHECK(rope.length(id) == values.size());
    CHECK(balanced(rope, values.size(), id));
    for (std::size_t i = 0; i < values.size(); i += 1 + rng() % 7)
      CHECK(rope.at(id, static_cast<std::uint32_t>(i)) == values[i]);
    Spell::Cursor c;
    const std::size_t from = values.empty() ? 0 : rng() % values.size();
    c.seek(rope, id, static_cast<std::uint32_t>(from));
    for (std::size_t i = from; i != values.size(); ++i, c.next()) {
      CHECK(!c.done());
      if (c.done()) break;
      CHECK(c.value() == values[i]);
    }
    CHECK(c.done());
    held.push_back(std::move(values));
    ids.push_back(id);
  }
}

void two_ropes_share_the_prefix_they_share() {
  std::mt19937 rng(11);
  Spell rope;
  for (unsigned round = 0; round != 200; ++round) {
    std::vector<std::uint32_t> a, b;
    const unsigned head = rng() % 300;
    for (unsigned i = 0; i != head; ++i) a.push_back(rng() % 3);
    b = a;
    const unsigned tailA = rng() % 40, tailB = rng() % 40;
    for (unsigned i = 0; i != tailA; ++i) a.push_back(rng() % 3);
    for (unsigned i = 0; i != tailB; ++i) b.push_back(rng() % 3);
    Spell::Id ra = Spell::none, rb = Spell::none;
    const auto split_into = [&](const std::vector<std::uint32_t>& v) {
      Spell::Id r = Spell::none;
      std::size_t i = 0;
      while (i != v.size()) {
        const std::size_t n =
            std::min<std::size_t>(v.size() - i, 1 + rng() % 40);
        r = rope.concat(r, chunk(rope, std::vector<std::uint32_t>(
                                           v.begin() + i, v.begin() + i + n)));
        i += n;
      }
      return r;
    };
    ra = split_into(a);
    rb = split_into(b);
    const std::size_t oa = rng() % (std::min(a.size(), b.size()) + 1);
    const std::size_t ob = oa;
    const std::size_t n = std::min(a.size() - oa, b.size() - ob);
    std::size_t expected = 0;
    while (expected != n && a[oa + expected] == b[ob + expected]) ++expected;
    CHECK(rope.common(ra, static_cast<std::uint32_t>(oa), rb,
                      static_cast<std::uint32_t>(ob),
                      static_cast<std::uint32_t>(n)) == expected);
    const std::size_t sa = a.empty() ? 0 : rng() % (a.size() + 1);
    const std::size_t sb = b.empty() ? 0 : rng() % (b.size() + 1);
    const std::size_t m = std::min(a.size() - sa, b.size() - sb);
    std::size_t e2 = 0;
    while (e2 != m && a[sa + e2] == b[sb + e2]) ++e2;
    CHECK(rope.common(ra, static_cast<std::uint32_t>(sa), rb,
                      static_cast<std::uint32_t>(sb),
                      static_cast<std::uint32_t>(m)) == e2);
  }
}

void parts_join_in_order_whatever_they_meet_first() {
  std::mt19937 rng(3);
  Spell rope;
  for (unsigned round = 0; round != 200; ++round) {
    std::vector<std::uint32_t> values;
    std::vector<Spell::Id> parts;
    for (unsigned i = 0, n = 1 + rng() % 40; i != n; ++i) {
      std::vector<std::uint32_t> piece;
      for (unsigned k = 0, m = rng() % (i % 2 == 0 ? 3 : 200); k != m; ++k)
        piece.push_back(rng() % 4);
      values.insert(values.end(), piece.begin(), piece.end());
      parts.push_back(chunk(rope, piece));
    }
    const Spell::Id joined = rope.concat_all(parts);
    CHECK(rope.length(joined) == values.size());
    CHECK(balanced(rope, values.size(), joined));
    Spell::Cursor c;
    c.seek(rope, joined, 0);
    for (std::size_t i = 0; i != values.size(); ++i, c.next())
      CHECK(!c.done() && c.value() == values[i]);
  }
}

void a_long_run_appended_or_prepended_stays_shallow() {
  Spell rope;
  Spell::Id back = Spell::none, front = Spell::none;
  constexpr std::uint32_t kCount = 100000;
  for (std::uint32_t i = 0; i != kCount; ++i) {
    back = rope.concat(back, rope.leaf(i));
    front = rope.concat(rope.leaf(i), front);
  }
  CHECK(balanced(rope, kCount, back) && balanced(rope, kCount, front));
  CHECK(rope.at(back, 77777) == 77777);
  CHECK(rope.at(front, 77777) == kCount - 1 - 77777);
  CHECK(rope.nodes() < 64 * std::size_t{kCount});
}

void forks_are_found_by_position() {
  std::mt19937 rng(5);
  Forks rope;
  Forks::Id r = Forks::none;
  std::vector<std::uint32_t> places;
  std::uint32_t at = 0;
  for (unsigned i = 0; i != 2000; ++i) {
    at += rng() % 3;
    places.push_back(at);
    r = rope.concat(r, rope.leaf(i, LrGssForkAgg{at}));
  }
  for (unsigned probe = 0; probe != 200; ++probe) {
    const std::uint32_t p = rng() % (at + 3);
    const auto cut =
        rope.first_where(r, [p](const LrGssForkAgg& a) { return a.at >= p; });
    const auto expected = static_cast<std::uint32_t>(
        std::ranges::lower_bound(places, p) - places.begin());
    CHECK(cut == expected);
  }
}

// A node is made once: what is joined alike is the same node, whichever way
// it was reached, and a value that differs is a node of its own.
void what_is_joined_alike_is_one_node() {
  Spell rope;
  const std::vector<std::uint32_t> values{3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
  const auto a = chunk(rope, values);
  const std::size_t made = rope.nodes();
  const auto b = chunk(rope, values);
  CHECK(a == b);
  CHECK(rope.nodes() == made);
  auto other = values;
  other[7] = 7;
  CHECK(chunk(rope, other) != a);
  CHECK(rope.leaf(4) == rope.leaf(4));
  CHECK(rope.leaf(4) != rope.leaf(5));
  Forks forks;
  CHECK(forks.leaf(9, LrGssForkAgg{2}) != forks.leaf(9, LrGssForkAgg{3}));
  CHECK(forks.leaf(9, LrGssForkAgg{2}) == forks.leaf(9, LrGssForkAgg{2}));
}

void virtual_lengths_are_checked_before_they_wrap() {
  Spell rope;
  const auto leaf = rope.leaf(1);
  auto prefix = leaf;
  for (unsigned level = 0; level != 63; ++level)
    prefix = rope.concat(prefix, prefix);
  CHECK(rope.length(prefix) == (Spell::Length{1} << 63));
  bool refused = false;
  try {
    (void)rope.concat(prefix, prefix);
  } catch (const std::length_error&) {
    refused = true;
  }
  CHECK(refused);
  CHECK(rope.at(prefix, rope.length(prefix) - 1) == 1);
  rope.clear();
  CHECK(rope.length(rope.leaf(2)) == 1);
}

void a_deep_shared_avl_tree_keeps_its_cursor_and_frontier_in_bounds() {
  Spell rope;
  const auto leaf = rope.leaf(7);
  auto previous = leaf, root = rope.concat(leaf, leaf);
  for (unsigned height = 2; height <= 90; ++height) {
    const auto next = rope.concat(root, previous);
    previous = root;
    root = next;
  }
  CHECK(rope.depth(root) == 90);
  CHECK(rope.length(root) == 7540113804746346429ull);
  Spell::Cursor cursor;
  cursor.seek(rope, root, 0);
  CHECK(!cursor.done() && cursor.value() == 7);
  cursor.next();
  CHECK(!cursor.done() && cursor.value() == 7);
  // Different roots force common() to descend both frontiers to a leaf.
  CHECK(rope.common(root, 0, leaf, 0, 1) == 1);
  CHECK(rope.common(leaf, 0, root, 0, 1) == 1);
}

// Two ropes of one content, joined in different shapes, are told alike; and
// the roots of one sequence read as their join.
void ropes_of_one_content_agree_whatever_their_shape() {
  std::mt19937 rng(13);
  Spell rope;
  for (unsigned round = 0; round != 100; ++round) {
    std::vector<std::uint32_t> values;
    for (unsigned i = 0, n = 1 + rng() % 400; i != n; ++i)
      values.push_back(rng() % 2);
    const auto left_leaning = [&] {
      Spell::Id r = Spell::none;
      for (const std::uint32_t v : values) r = rope.concat(r, rope.leaf(v));
      return r;
    }();
    const auto halved = [&] {
      std::vector<Spell::Id> leaves;
      for (const std::uint32_t v : values) leaves.push_back(rope.leaf(v));
      return rope.concat_all(leaves);
    }();
    CHECK(rope.common(left_leaning, 0, halved, 0, values.size()) ==
          values.size());
    const std::size_t cut = rng() % (values.size() + 1);
    const std::vector<std::uint32_t> head(values.begin(), values.begin() + cut);
    const std::vector<std::uint32_t> tail(values.begin() + cut, values.end());
    const std::array<Spell::Id, 2> roots{chunk(rope, head), chunk(rope, tail)};
    const std::size_t start = rng() % (values.size() + 1);
    CHECK(rope.common(roots, start, std::span(&halved, 1), start,
                      values.size() - start) == values.size() - start);
  }
}

// Any number of roots read as their join.
void many_roots_read_as_one_sequence() {
  std::mt19937 rng(17);
  Spell rope;
  std::vector<std::uint32_t> values;
  std::vector<Spell::Id> roots;
  for (unsigned i = 0; i != 300; ++i) {
    std::vector<std::uint32_t> piece;
    for (unsigned k = 0, m = 1 + rng() % 9; k != m; ++k)
      piece.push_back(rng() % 3);
    values.insert(values.end(), piece.begin(), piece.end());
    roots.push_back(chunk(rope, piece));
  }
  std::vector<Spell::Id> joined{roots};
  const auto whole = rope.concat_all(joined);
  for (unsigned probe = 0; probe != 50; ++probe) {
    const std::size_t from = rng() % (values.size() + 1);
    CHECK(rope.common(roots, from, std::span(&whole, 1), from,
                      values.size() - from) == values.size() - from);
    auto other = values;
    if (from < other.size()) other[from] ^= 1;
    const auto changed = chunk(rope, other);
    CHECK(rope.common(roots, from, std::span(&changed, 1), from,
                      values.size() - from) == 0);
  }
}

// A polynomial hash of the values modulo 2^61 - 1 at a fixed base: two values
// moved by amounts that cancel it leave it as it was.
struct Poly {
  static constexpr std::uint64_t kMod = (std::uint64_t{1} << 61) - 1;
  static constexpr std::uint64_t kBase = 0x1D8E4E27C47D124Full;
  static std::uint64_t mul(std::uint64_t a, std::uint64_t b) {
    return static_cast<std::uint64_t>(static_cast<unsigned __int128>(a) * b %
                                      kMod);
  }
  static std::uint64_t of(const std::vector<std::uint32_t>& values) {
    std::uint64_t h = 0;
    for (const std::uint32_t v : values) h = (mul(h, kBase) + v + 1) % kMod;
    return h;
  }
};

// The shortest (a, b) with a * base^gap + b a multiple of the modulus.
std::pair<std::int64_t, std::int64_t> cancelling_pair(unsigned gap) {
  using Wide = __int128;
  std::uint64_t c = 1;
  for (unsigned i = 0; i != gap; ++i) c = Poly::mul(c, Poly::kBase);
  std::array<std::array<std::int64_t, 2>, 2> basis{
      {{1, static_cast<std::int64_t>(Poly::kMod - c)},
       {0, static_cast<std::int64_t>(Poly::kMod)}}};
  const auto dot = [](const auto& p, const auto& q) {
    return static_cast<Wide>(p[0]) * q[0] + static_cast<Wide>(p[1]) * q[1];
  };
  const auto rounded = [](Wide n, Wide d) {
    Wide q = n / d;
    const Wide r = n - q * d;
    if (2 * (r < 0 ? -r : r) > d) q += r < 0 ? -1 : 1;
    return q;
  };
  for (;;) {
    if (dot(basis[0], basis[0]) > dot(basis[1], basis[1]))
      std::swap(basis[0], basis[1]);
    const Wide mu = rounded(dot(basis[0], basis[1]), dot(basis[0], basis[0]));
    if (mu == 0) break;
    basis[1][0] -= static_cast<std::int64_t>(mu * basis[0][0]);
    basis[1][1] -= static_cast<std::int64_t>(mu * basis[0][1]);
  }
  return {basis[0][0], basis[0][1]};
}

// Two sequences that one hash cannot tell apart are two sequences here.
void sequences_that_collide_under_a_polynomial_hash_are_told_apart() {
  constexpr unsigned kGap = 7, kLength = 24, kFirst = 5;
  const auto [a, b] = cancelling_pair(kGap);
  CHECK(a != 0 && b != 0);
  CHECK(std::llabs(a) < (std::int64_t{1} << 31) &&
        std::llabs(b) < (std::int64_t{1} << 31));
  std::vector<std::uint32_t> x(kLength, 1u << 31), y = x;
  y[kFirst] = static_cast<std::uint32_t>(y[kFirst] + a);
  y[kFirst + kGap] = static_cast<std::uint32_t>(y[kFirst + kGap] + b);
  CHECK(x != y && Poly::of(x) == Poly::of(y));
  Spell rope;
  CHECK(rope.common(chunk(rope, x), 0, chunk(rope, y), 0, kLength) == kFirst);
  CHECK(rope.common(chunk(rope, y), 0, chunk(rope, x), 0, kLength) == kFirst);
}

}  // namespace

namespace gss_pool {
namespace {

struct Cell {
  std::uint32_t at = 7;
  std::uint16_t twice = 0;
};

void a_reference_survives_the_growth_that_follows() {
  cursiv::LrGssPool<Cell, 4> pool;
  const std::size_t first = pool.push_back({1, 2});
  const Cell* held = &pool[first];
  for (std::uint32_t i = 2; i != 1000; ++i)
    CHECK(pool.push_back({i, static_cast<std::uint16_t>(2 * i)}) == i - 1);
  CHECK(pool.size() == 999);
  CHECK(&pool[first] == held && held->at == 1 && held->twice == 2);
  for (std::uint32_t i = 2; i != 1000; ++i)
    CHECK(pool[i - 1].at == i && pool[i - 1].twice == 2 * i);
}

void a_cleared_pool_starts_over() {
  cursiv::LrGssPool<Cell, 4> pool;
  for (std::uint32_t i = 0; i != 100; ++i) (void)pool.push_back({i, 0});
  pool.clear();
  CHECK(pool.size() == 0);
  CHECK(pool.push_back({5, 6}) == 0);
  CHECK(pool[0].at == 5 && pool[0].twice == 6);
}

}  // namespace

void run() {
  a_reference_survives_the_growth_that_follows();
  a_cleared_pool_starts_over();
}
}  // namespace gss_pool

namespace gss_runs {
namespace {

using Runs = cursiv::LrGssRuns<std::uint32_t, 3>;

void every_run_stays_in_one_piece_whatever_its_length() {
  Runs runs;
  std::vector<std::pair<std::size_t, std::vector<std::uint32_t>>> held;
  std::uint32_t next = 1;
  for (const std::size_t length : {3u, 0u, 5u, 8u, 9u, 1u, 20u, 2u, 7u, 8u}) {
    std::vector<std::uint32_t> run;
    for (std::size_t i = 0; i != length; ++i) run.push_back(next++);
    held.emplace_back(runs.append(run), run);
  }
  std::size_t total = 0;
  for (const auto& [at, run] : held) {
    total += run.size();
    const auto seen = runs.span(at, run.size());
    CHECK(std::equal(seen.begin(), seen.end(), run.begin(), run.end()));
  }
  CHECK(runs.size() == total);
}

void a_run_filled_alike_reads_alike() {
  Runs runs;
  const std::size_t at = runs.append(11, 42u);
  for (std::size_t i = 0; i != 11; ++i) CHECK(runs[at + i] == 42u);
  const std::array<std::uint32_t, 2> pair{1, 2};
  const std::size_t later = runs.append(pair);
  CHECK(later >= at + 11 && runs[later] == 1 && runs[later + 1] == 2);
}

void a_cleared_run_store_starts_over() {
  Runs runs;
  for (unsigned i = 0; i != 50; ++i) (void)runs.append(5, i);
  runs.clear();
  CHECK(runs.size() == 0);
  CHECK(runs.append(3, 9u) == 0 && runs[2] == 9u);
}

}  // namespace

void run() {
  every_run_stays_in_one_piece_whatever_its_length();
  a_run_filled_alike_reads_alike();
  a_cleared_run_store_starts_over();
}
}  // namespace gss_runs

namespace gss_index {
namespace {

using Id = cursiv::LrGssIndex::Id;
using Key = std::array<std::uint32_t, 11>;

Key key(std::uint32_t value) {
  Key k{};
  k.fill(17);
  k.back() = value;
  return k;
}

std::uint64_t collision_hash(const Key& k) {
  return k.back() % 2 == 0 ? 0 : std::numeric_limits<std::uint64_t>::max();
}

void collisions_and_moving_pools_preserve_the_first_id() {
  cursiv::LrGssIndex index;
  std::vector<Key> pool(11);
  constexpr Id count = 1024;
  unsigned deadCalls = 0;
  const auto intern = [&](const Key& k) {
    const Id fresh = static_cast<Id>(pool.size());
    const Id id = index.intern(
        collision_hash(k), fresh,
        [&](Id candidate) {
          CHECK(candidate < pool.size());
          return candidate < pool.size() && pool[candidate] == k;
        },
        [&](Id) {
          ++deadCalls;
          return false;
        });
    if (id == fresh) pool.push_back(k);
    return id;
  };

  for (Id i = 0; i < count; ++i) CHECK(intern(key(i)) == 11 + i);
  CHECK(index.size() == count);
  for (Id i = 0; i < count; ++i) {
    const Id at = (i * 617 + 13) % count;
    CHECK(intern(key(at)) == 11 + at);
  }
  CHECK(index.size() == count && pool.size() == 11 + count);

  index.clear();
  pool.clear();
  CHECK(index.size() == 0);
  for (Id i = 0; i < count; ++i) CHECK(intern(key(count + i)) == i);
  for (Id i = count; i-- > 0;) CHECK(intern(key(count + i)) == i);
  CHECK(index.size() == count && pool.size() == count);
  CHECK(deadCalls == 0);
}

void shedding_keeps_live_collisions_and_counts_only_retained_ids() {
  constexpr Id count = 2048;
  const auto hash = [](Id key) {
    return std::uint64_t{key % 64} * 0x9E3779B97F4A7C15ull;
  };
  // Retaining three quarters grows the table; retaining one quarter rebuilds
  // it in place. Both paths must discard dead ids and preserve live matches.
  for (const Id discard : {512u, 1536u}) {
    cursiv::LrGssIndex index;
    Id deadBefore = 0;
    unsigned deadCalls = 0;
    const auto dead = [&](Id id) {
      ++deadCalls;
      return id < deadBefore;
    };
    for (Id i = 0; i < count; ++i)
      CHECK(index.intern(hash(i), i, [&](Id id) { return id == i; }, dead) == i);
    CHECK(index.size() == count && deadCalls == 0);

    deadBefore = discard;
    CHECK(index.intern(hash(count), count,
                       [&](Id id) { return id == count; }, dead) == count);
    CHECK(index.size() == count - discard + 1 && deadCalls != 0);
    const unsigned afterShed = deadCalls;
    for (Id i = discard; i <= count; ++i)
      CHECK(index.intern(hash(i), count + 1,
                         [&](Id id) { return id == i; }, dead) == i);
    CHECK(index.size() == count - discard + 1 && deadCalls == afterShed);

    // A discarded key receives a fresh id; a second lookup keeps that id.
    CHECK(index.intern(hash(0), count + 1, [](Id id) { return id == 0; }, dead) ==
          count + 1);
    CHECK(index.intern(hash(0), count + 2,
                       [&](Id id) { return id == count + 1; }, dead) == count + 1);
    CHECK(index.size() == count - discard + 2);
    index.clear();
    CHECK(index.size() == 0);
    for (Id i = 0; i < 64; ++i)
      CHECK(index.intern(hash(i), i, [](Id) { return false; }, dead) == i);
    CHECK(index.size() == 64 && deadCalls == afterShed);
  }
}

void moved_indexes_preserve_ids_and_their_sources_can_be_reused() {
  cursiv::LrGssIndex source;
  constexpr Id count = 32;
  const auto never = [](Id) { return false; };
  for (Id i = 0; i < count; ++i) CHECK(source.intern(i % 2, i, never) == i);
  cursiv::LrGssIndex moved(std::move(source));
  CHECK(moved.size() == count);
  for (Id i = 0; i < count; ++i)
    CHECK(moved.intern(i % 2, count, [&](Id id) { return id == i; }) == i);
  CHECK(source.intern(0, 100, never) == 100);
  CHECK(source.intern(0, 101, [](Id id) { return id == 100; }) == 100);
  CHECK(source.size() == 1);

  cursiv::LrGssIndex assigned;
  CHECK(assigned.intern(0, 200, never) == 200);
  assigned = std::move(moved);
  CHECK(assigned.size() == count);
  for (Id i = 0; i < count; ++i)
    CHECK(assigned.intern(i % 2, count, [&](Id id) { return id == i; }) == i);
  CHECK(moved.intern(1, 300, never) == 300);
  CHECK(moved.size() == 1);
}

void hash_extremes_and_nonsequential_ids_are_values() {
  cursiv::LrGssIndex index;
  constexpr Id last = std::numeric_limits<Id>::max() - 1;
  const auto never = [](Id) { return false; };
  CHECK(index.intern(0, last, never) == last);
  CHECK(index.intern(std::numeric_limits<std::uint64_t>::max(), 0, never) == 0);
  CHECK(index.intern(0, 7, [](Id id) { return id == last; }) == last);
  CHECK(index.intern(std::numeric_limits<std::uint64_t>::max(), 7,
                     [](Id id) { return id == 0; }) == 0);
  CHECK(index.size() == 2);
  index.clear();
  index.clear();
  CHECK(index.intern(0, 0, never) == 0);
  CHECK(index.size() == 1);
}

}  // namespace

void run() {
  collisions_and_moving_pools_preserve_the_first_id();
  shedding_keeps_live_collisions_and_counts_only_retained_ids();
  moved_indexes_preserve_ids_and_their_sources_can_be_reused();
  hash_extremes_and_nonsequential_ids_are_values();
}
}  // namespace gss_index

int main() {
  gss_pool::run();
  gss_runs::run();
  gss_index::run();

  a_rope_reads_what_it_was_joined_from();
  two_ropes_share_the_prefix_they_share();
  parts_join_in_order_whatever_they_meet_first();
  a_long_run_appended_or_prepended_stays_shallow();
  forks_are_found_by_position();
  what_is_joined_alike_is_one_node();
  virtual_lengths_are_checked_before_they_wrap();
  a_deep_shared_avl_tree_keeps_its_cursor_and_frontier_in_bounds();
  ropes_of_one_content_agree_whatever_their_shape();
  many_roots_read_as_one_sequence();
  sequences_that_collide_under_a_polynomial_hash_are_told_apart();
  return cursiv::test::report("LrGssRope_test");
}
