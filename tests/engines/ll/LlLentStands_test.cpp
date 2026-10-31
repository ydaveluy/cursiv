// The walks over the frames an episode lends from the descent's stack stop at
// the first of them: what a stack stands on there is answered from the depth
// the lending recorded. Every answer is the one the walk to the bottom gives,
// and a descent a hundred thousand frames deep costs no more than a shallow
// one.

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "cursiv/engines/ll/runtime/LlEpisodeCache.hpp"
#include "cursiv/test/Check.hpp"

namespace cursiv {

// The walks under test are private to the sim.
struct LlLentStandsProbe {
  static LlStackSet& stacks(LlEpisodeSim& sim) { return sim._stacks; }
  static bool stands_on(LlEpisodeSim& sim, std::uint32_t a, std::uint32_t b) {
    return sim.stands_on(a, b);
  }
  static bool member_related(LlEpisodeSim& sim, std::uint32_t cell,
                             std::uint32_t stack) {
    return sim.member_related(cell, stack);
  }
  static bool related(LlEpisodeSim& sim, std::uint32_t a, std::uint32_t b) {
    return sim.related(a, b);
  }
  static bool rung(const LlEpisodeSim& sim, std::uint32_t stack) {
    return sim.rung(stack) != nullptr;
  }
};

}  // namespace cursiv

namespace {

using namespace cursiv;
using Probe = LlLentStandsProbe;

struct Fixture {
  LlEpisodeCache cache;
  LlAtnView atn;
  std::vector<std::uint32_t> lent;

  Fixture() {
    cache.exact.emplace();
    cache.lent.emplace();
  }

  // A stack of `depth` frames in the exact pool over `base`, lent.
  std::uint32_t grow(std::uint32_t base, unsigned depth, std::uint32_t ret0) {
    LlCtxPool& exact = cache.exact->contexts();
    std::uint32_t at = base;
    for (unsigned i = 0; i < depth; ++i) {
      at = exact.push(at, ret0 + i);
      lent.push_back(ll_lend_stack(cache, at));
    }
    return at;
  }
};

// The same cells, made in the same order, in each sim.
struct World {
  std::vector<std::uint32_t> stacks;
  std::vector<std::uint32_t> cells;
};

World populate(LlEpisodeSim& sim, LlCtxPool& pool,
               const std::vector<std::uint32_t>& lent) {
  World w;
  w.stacks.push_back(kLlCtxNone);
  for (std::size_t i = 0; i < lent.size(); i += 7) w.stacks.push_back(lent[i]);
  w.stacks.push_back(lent.back());
  // Frames the walk makes: over the frames lent, and over each other.
  std::vector<std::uint32_t> own;
  std::uint32_t tag = 9000;
  for (std::size_t i = 0; i < lent.size(); i += 11) {
    const std::uint32_t one = pool.push(lent[i], tag++);
    const std::uint32_t two = pool.push(one, tag++);
    const std::uint32_t over_none = pool.push(kLlCtxNone, tag++);
    own.insert(own.end(), {one, two, over_none});
  }
  w.stacks.insert(w.stacks.end(), own.begin(), own.end());
  const auto member = [](std::uint32_t stack) {
    return LlStackSet::Member{stack, LlHistory::kRoot, LlHistory::kRoot};
  };
  // Cells of one, two and many members, mixing the kinds.
  const std::size_t n = w.stacks.size();
  for (std::size_t i = 0; i + 1 < n; i += 3) {
    const std::uint32_t cell =
        Probe::stacks(sim).fork(member(w.stacks[i]), member(w.stacks[i + 1]));
    w.cells.push_back(cell);
  }
  for (std::size_t i = 0; i < n; i += 5)
    w.cells.push_back(Probe::stacks(sim).open(member(w.stacks[i])));
  {
    std::uint32_t wide = Probe::stacks(sim).open(member(w.stacks[0]));
    for (std::size_t i = 1; i < n; i += 2)
      Probe::stacks(sim).add(wide, member(w.stacks[i]));
    w.cells.push_back(wide);
    std::uint32_t lentOnly = Probe::stacks(sim).open(member(lent.front()));
    for (std::size_t i = 0; i < lent.size(); i += 13)
      Probe::stacks(sim).add(lentOnly, member(lent[i]));
    w.cells.push_back(lentOnly);
    std::uint32_t deepOnly = Probe::stacks(sim).open(member(lent.back()));
    for (std::size_t i = 0; i < 20; ++i)
      Probe::stacks(sim).add(deepOnly, member(pool.push(lent.back(), 7000 + i)));
    w.cells.push_back(deepOnly);
  }
  // A cell that stands over a cell.
  w.cells.push_back(Probe::stacks(sim).fork(member(w.cells[0]), member(w.stacks[2])));
  w.stacks.insert(w.stacks.end(), w.cells.begin(), w.cells.end());
  return w;
}

void the_answers_are_those_of_the_walk_to_the_bottom() {
  Fixture f;
  // A trunk, and a branch that leaves it a third of the way up.
  const std::uint32_t trunk = f.grow(kLlCtxNone, 90, 100);
  (void)trunk;
  std::uint32_t at = kLlCtxNone;
  for (unsigned i = 0; i < 30; ++i)
    at = f.cache.exact->contexts().push(at, 100 + i);
  f.grow(at, 40, 5000);
  f.grow(kLlCtxNone, 12, 8000);
  CHECK(f.cache.lentRung.size() > 100);

  LlCtxPool& pool = f.cache.lent->contexts();
  LlEpisodeSim fast(f.atn, *f.cache.lent, false);
  LlEpisodeSim slow(f.atn, *f.cache.lent, false);
  fast.read_lent_rungs(&f.cache.lentRung);
  CHECK(pool.begin_scratch());
  const World a = populate(fast, pool, f.lent);
  const World b = populate(slow, pool, f.lent);
  CHECK(a.stacks == b.stacks && a.cells == b.cells);
  // The fast sim knows the frames lent, the slow one walks them down.
  CHECK(Probe::rung(fast, f.lent[5]) && !Probe::rung(slow, f.lent[5]));
  CHECK(!Probe::rung(fast, kLlCtxNone));

  std::size_t asked = 0, held = 0;
  for (const std::uint32_t x : a.stacks)
    for (const std::uint32_t y : a.stacks) {
      const bool want = Probe::stands_on(slow, x, y);
      CHECK(Probe::stands_on(fast, x, y) == want);
      CHECK(Probe::related(fast, x, y) == Probe::related(slow, x, y));
      ++asked;
      held += want ? 1 : 0;
    }
  CHECK(held > a.stacks.size() && held < asked);
  for (const std::uint32_t cell : a.cells)
    for (const std::uint32_t y : a.stacks)
      CHECK(Probe::member_related(fast, cell, y) == Probe::member_related(slow, cell, y));
  // A cell asked again, once more members have joined it.
  for (const std::uint32_t cell : a.cells) {
    const LlStackSet::Member extra{f.lent[3], LlHistory::kRoot,
                                   LlHistory::kRoot};
    Probe::stacks(fast).add(cell, extra);
    Probe::stacks(slow).add(cell, extra);
    for (const std::uint32_t y : a.stacks)
      CHECK(Probe::member_related(fast, cell, y) == Probe::member_related(slow, cell, y));
  }
  pool.end_scratch();
}

void a_descent_a_hundred_thousand_deep_costs_its_questions_alone() {
  Fixture f;
  constexpr unsigned kDepth = 100000;
  f.grow(kLlCtxNone, kDepth, 1);
  LlCtxPool& pool = f.cache.lent->contexts();
  LlEpisodeSim sim(f.atn, *f.cache.lent, false);
  sim.read_lent_rungs(&f.cache.lentRung);
  CHECK(pool.begin_scratch());
  // A frame the walk made on the tip, and one over the unknown caller.
  const std::uint32_t overTip = pool.push(f.lent.back(), 424242);
  const std::uint32_t apart = pool.push(kLlCtxNone, 424243);
  std::mt19937 rng(7);
  std::uniform_int_distribution<unsigned> pick(0, kDepth - 1);
  const auto member = [](std::uint32_t stack) {
    return LlStackSet::Member{stack, LlHistory::kRoot, LlHistory::kRoot};
  };
  // Cells of frames at depths spread over the trunk, which a frame lent is
  // always related to: the frames lent stand on one another.
  std::vector<std::uint32_t> cells;
  for (unsigned c = 0; c < 200; ++c) {
    cells.push_back(Probe::stacks(sim).open(member(f.lent[pick(rng)])));
    for (unsigned i = 0; i < 20; ++i)
      Probe::stacks(sim).add(cells.back(), member(f.lent[pick(rng)]));
  }
  for (unsigned q = 0; q < 20000; ++q) {
    const unsigned i = pick(rng), j = pick(rng);
    // Depth is the index: the frame lent at i stands on the one at j if it
    // is not the shallower.
    CHECK(Probe::stands_on(sim, f.lent[i], f.lent[j]) == (i >= j));
    CHECK(Probe::stands_on(sim, overTip, f.lent[j]));
    CHECK(!Probe::stands_on(sim, f.lent[j], overTip));
    CHECK(Probe::stands_on(sim, f.lent[i], kLlCtxNone));
    CHECK(!Probe::stands_on(sim, f.lent[i], apart));
    CHECK(!Probe::stands_on(sim, apart, f.lent[i]));
    const std::uint32_t cell = cells[q % cells.size()];
    CHECK(Probe::member_related(sim, cell, f.lent[i]));
    CHECK(Probe::member_related(sim, cell, overTip));
    // A frame over the unknown caller is on no chain of the cell.
    CHECK(!Probe::member_related(sim, cell, apart));
  }
  pool.end_scratch();
}

}  // namespace

int main() {
  the_answers_are_those_of_the_walk_to_the_bottom();
  a_descent_a_hundred_thousand_deep_costs_its_questions_alone();
  return cursiv::test::report("LlLentStands_test");
}
