// The parts of a repair search shared by both engines: what a reading holds
// for it, its budget, the paths, the queue, dominance between readings, the
// race and the election among the cheapest goals. An engine brings its
// readings and edits.
#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/shared/runtime/CancellationPoll.hpp"
#include "cursiv/shared/runtime/PathRank.hpp"
#include "cursiv/shared/runtime/RepairBudget.hpp"
#include "cursiv/shared/runtime/TiedInserts.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

// The reach of a goal that holds, or whose reading no refusal stopped.
inline constexpr TextOffset kRepairReachAll = 0xFFFFFFFFu;

// The steps a race between goals of one cost may take: past them, the text has
// said what it can of the fault, and their key and rank decide.
inline constexpr std::uint32_t kRepairRaceSteps = 1024;

// Where a point stands in the fault it is repairing, `[abandon] (ins)* (rep |
// del+)?`; the phases are in the order the fault goes through them.
enum class RepairPhase : std::uint8_t {
  // No edit yet at this point.
  beforeEdit,
  // The abandon is made: insertions, a replacement or deletions go on from it.
  afterAbandon,
  // An insertion is made, and more may follow where it stands.
  inserting,
  // Bytes were dropped or a trivia skipped: only more of them.
  discardOnly,
};

// What a slot of the search stands for. A generator is queued directly and
// never admitted: it has no future of its own, only edits of the reading it
// was made from not yet made.
enum class RepairWork : std::uint8_t {
  // A reading, read forward.
  read,
  // Abandons of a refused reading.
  enumerateAbandons,
  // The insertions of a refused reading that no required run reaches.
  enumerateInsertions,
  // The replacement of the word in hand by the keyword its reading is forced
  // to, at the cost of its deletion and the keyword's insertion.
  forcedReplacement,
};

// What a reading of either engine holds for the search: its key, where it
// stands in its fault and what its slot stands for.
struct RepairReading {
  // The key; the rank is read through `link`.
  std::uint32_t cost = 0;
  std::uint32_t lost = 0;
  std::uint32_t edits = 0;
  std::uint32_t faults = 0;
  std::uint32_t invented = 0;
  std::uint32_t link = kNoEditLink;
  // Real tokens since the last edit, as far as the engine's goal counts them.
  std::uint8_t shifts = 0;
  RepairPhase phase = RepairPhase::beforeEdit;
  RepairWork work = RepairWork::read;
  bool done = false;
  // No edit yet at this point: the next one opens a fault. A generator has
  // counted the fault it opens when it was made, and is never counted again.
  bool fresh = true;
  // Dominated, or an arm its own action refused: never read again.
  bool gone = false;
  // Named by a queue entry: its slot outlives the edit that created it.
  bool queued = false;

  bool generates() const { return work != RepairWork::read; }

  // Which edits may come next at the point: an abandon before any edit; a
  // replacement until bytes are dropped; an insertion until then too, but in
  // front of bytes no alphabet reads (`garbage`), one at most.
  bool mayAbandon() const { return phase == RepairPhase::beforeEdit; }
  bool mayReplace() const { return phase <= RepairPhase::inserting; }
  bool mayInsert(bool garbage) const {
    return phase < RepairPhase::inserting ||
           (phase == RepairPhase::inserting && !garbage);
  }
};

// A reading queued at `f`, with the rest of its key and the order it was
// queued in.
struct RepairEntry {
  std::uint32_t f, lost, edits, faults, invented, seq, at;
};

// Two keys in order: cost, then the author's tokens lost, the edits made
// (a run of insertions counts as one), the faults, the tokens invented.
template <class A, class B>
CURSIV_ALWAYS_INLINE inline std::strong_ordering repair_key_order(
    std::uint32_t costA, const A& a, std::uint32_t costB, const B& b) {
  const std::uint64_t ka = (std::uint64_t{costA} << 32) | a.lost;
  const std::uint64_t kb = (std::uint64_t{costB} << 32) | b.lost;
  if (ka != kb) return ka <=> kb;
  if (a.edits != b.edits) return a.edits <=> b.edits;
  return ((std::uint64_t{a.faults} << 32) | a.invented) <=>
         ((std::uint64_t{b.faults} << 32) | b.invented);
}

template <class R>
std::strong_ordering repair_key_cmp(const R& a, const R& b) {
  return repair_key_order(a.cost, a, b.cost, b);
}

// The queue hands out the least key first, then the first queued.
inline constexpr auto repair_entry_after = [](const RepairEntry& a,
                                              const RepairEntry& b) {
  const auto k = repair_key_order(a.f, a, b.f, b);
  return k != 0 ? k > 0 : a.seq > b.seq;
};

// Roots precede every edit, so their link ids are source ranks, also
// after an admission redirects a path through an earlier source.
template <class Edit>
std::uint32_t repair_root_of(const std::vector<Edit>& links, std::uint32_t l) {
  while (links[l].parent != kNoEditLink) l = links[l].parent;
  return l;
}

template <class Edit>
struct RepairSearch {
  RepairSearch(TextOffset len, const RepairBudget& b)
      : budget(repair_allot(b, len)), perSearch(b.perSearch) {}

  std::uint64_t budget;
  std::uint64_t perSearch;
  std::uint64_t work = 0;
  // Where the search in flight stops: its share of what the parse has left.
  std::uint64_t limit = 0;
  // The parse's budget is spent: every later fault is repaired by the first
  // edit that holds.
  bool degraded = false;
  std::vector<Edit> links;
  // Links a dominated reading handed to the one that kept its future.
  std::uint64_t handed = 0;
  PathRank rank;
  std::vector<RepairEntry> heap;
  std::uint32_t seq = 0;
  // The goals of the least cost.
  std::vector<std::uint32_t> goals;
  // What the elected fault's first insertion could as cheaply have been, and
  // the paths dropped for one as cheap.
  std::vector<ElementId> also;
  TiedInserts tied;

  std::uint32_t link(const Edit& g) {
    links.push_back(g);
    return static_cast<std::uint32_t>(links.size()) - 1;
  }

  // Reading `rs[at]`, as `r` stands, queued at `f` with `lost` more tokens
  // lost. One that took a token since its last edit opens a fault with the
  // next one: `opens`.
  template <class R>
  void queue(std::vector<R>& rs, std::uint32_t f, const R& r, std::uint32_t at,
             std::uint32_t lost, bool opens) {
    heap.push_back(RepairEntry{f, r.lost + lost, r.edits,
                               r.faults + (opens ? 1u : 0u), r.invented, seq++,
                               at});
    std::push_heap(heap.begin(), heap.end(), repair_entry_after);
    rs[at].queued = true;
  }

  // A copy of generator `from` queued at the least its edits cost, `extra`
  // more than its reading: it counts the edit and the fault it opens here.
  template <class R>
  void defer(std::vector<R>& rs, const R& from, std::uint32_t extra) {
    R& g = rs.emplace_back(from);
    g.cost += extra;
    g.edits += 1;
    g.faults += g.fresh ? 1u : 0u;
    queue(rs, g.cost, g, static_cast<std::uint32_t>(rs.size()) - 1, 0, false);
  }

  RepairEntry pop() {
    std::pop_heap(heap.begin(), heap.end(), repair_entry_after);
    const RepairEntry e = heap.back();
    heap.pop_back();
    return e;
  }

  // Starts a new search, at its share of what the parse has left.
  void restart() {
    links.clear();
    heap.clear();
    goals.clear();
    also.clear();
    tied.clear();
    seq = 0;
    limit = work < budget ? work + std::min(perSearch, budget - work) : budget;
  }
};

// Whether reading `rs[i]` dies to `rs[kept]`, the one its future keeps: at a
// worse key and no more allowance `A`, or at an equal key and allowance, where
// the kept one takes the better path. One that outdoes it is kept instead.
template <auto A, class G, class R, class Kind>
CURSIV_ALWAYS_INLINE inline bool repair_merge(RepairSearch<G>& s,
                                              std::vector<R>& rs,
                                              std::uint32_t i,
                                              std::uint32_t& kept,
                                              Kind&& kind) {
  R& r = rs[i];
  R& x = rs[kept];
  const auto k = x.gone ? std::strong_ordering::less : repair_key_cmp(r, x);
  if (k > 0 ? r.*A <= x.*A : k == 0 && r.*A == x.*A) {
    if (k == 0) {
      const PathRank::Parting p = s.rank.part(s.links, r.link, x.link);
      const bool handed = r.link != x.link && PathRank::before(s.links, p);
      s.work += s.tied.tie(s.links, x.link, r.link, p, handed, kind);
      if (handed) {
        s.links[x.link] = s.links[r.link];
        ++s.handed;
      }
    }
    r.gone = true;
    return true;
  }
  if (x.gone || (k < 0 && x.*A <= r.*A)) {
    x.gone = true;
    kept = i;
  }
  return false;
}

// What a step leaves of a reading: past a token, inside one's actions, refused,
// at the end of the text, or unread where the share ran out.
enum class RepairRead : std::uint8_t { shifted, inside, refused, done, spent };

// The goals read on with no edit: `reach[k]` is where candidate `k`'s furthest
// reading was refused, or `kRepairReachAll`; `of[g]` is goal `g`'s candidate.
// The reading furthest back steps until all agree or `e.kSteps` pass; true if
// the share ran out. Engine `e` steps a reading, says how it and its forks
// stand, and joins the readings that meet past a token.
template <class G, class R, class E>
bool repair_race(const RepairSearch<G>& s, std::vector<R>& rs,
                 const std::vector<std::uint32_t>& of,
                 std::vector<TextOffset>& reach, const CancellationPoll& stop,
                 E e) {
  // Reading `rs[base + k]` races for the candidates `ofs[k]`.
  const auto base = static_cast<std::uint32_t>(rs.size());
  std::vector<std::vector<std::uint32_t>> ofs;
  std::vector<std::uint32_t> queue;
  const auto behind = [&](std::uint32_t a, std::uint32_t b) {
    return e.pos(rs[a]) > e.pos(rs[b]);
  };
  const auto enqueue = [&](std::uint32_t i) {
    queue.push_back(i);
    std::ranges::push_heap(queue, behind);
  };
  std::ranges::fill(reach, 0);
  for (std::uint32_t g = 0; g < s.goals.size(); ++g) {
    if (rs[s.goals[g]].done) {
      reach[of[g]] = kRepairReachAll;
      continue;
    }
    const R r = rs[s.goals[g]];
    rs.push_back(r);
    ofs.push_back({of[g]});
    enqueue(base + static_cast<std::uint32_t>(ofs.size()) - 1);
  }
  const auto reach_all = [&](std::uint32_t i) {
    for (const std::uint32_t k : ofs[i - base]) reach[k] = kRepairReachAll;
  };
  const auto level = [&] {
    const std::vector<std::uint32_t>& first = ofs[queue[0] - base];
    TextOffset least = kRepairReachAll;
    for (const std::uint32_t q : queue) {
      if (ofs[q - base] != first) return false;
      least = std::min(least, e.pos(rs[q]));
    }
    for (std::uint32_t k = 0; k < reach.size(); ++k)
      if (!std::ranges::binary_search(first, k) && reach[k] >= least)
        return false;
    return true;
  };
  const auto place = [&](std::uint32_t i) {
    switch (e.stands(i)) {
      case RepairRead::refused: {
        const TextOffset at = e.refused(i);
        for (const std::uint32_t k : ofs[i - base])
          reach[k] = std::max(reach[k], at);
        return;
      }
      case RepairRead::done:
        reach_all(i);
        return;
      case RepairRead::shifted:
        if (const std::uint32_t kept = e.join(i); kept != i) {
          std::vector<std::uint32_t> both;
          std::ranges::set_union(ofs[kept - base], ofs[i - base],
                                 std::back_inserter(both));
          ofs[kept - base] = std::move(both);
          return;
        }
        [[fallthrough]];
      default:
        enqueue(i);
    }
  };
  std::vector<std::uint32_t> forks;
  std::uint32_t steps = 0;
  while (!queue.empty()) {
    stop.poll();
    const bool decided = steps >= e.kSteps || level();
    if (decided || e.spent()) {
      for (const std::uint32_t q : queue) reach_all(q);
      return !decided;
    }
    std::ranges::pop_heap(queue, behind);
    const std::uint32_t i = queue.back();
    queue.pop_back();
    steps += e.step(i, forks);
    if (e.stands(i) == RepairRead::spent) {
      reach_all(i);
      for (const std::uint32_t q : queue) reach_all(q);
      return true;
    }
    for (const std::uint32_t j : forks) {
      assert(j == base + ofs.size());
      ofs.push_back(std::vector(ofs[i - base]));
      place(j);
    }
    place(i);
  }
  return false;
}

// Elects by the furthest reach, then the key, then the rank; goals that make
// the same edits race as one. Names in `also` what ties with the winner's
// first insertion, and reads `stop` before each walk of a path.
template <auto Fold, auto Same, auto Kind, class G, class R, class Race>
std::uint32_t repair_elect(RepairSearch<G>& s, const std::vector<R>& rs,
                           bool roots, const CancellationPoll& stop,
                           Race&& race) {
  // No goal is gone: one dies only to a goal of lesser key, popped first.
  assert(!s.goals.empty() &&
         std::ranges::none_of(s.goals,
                              [&](std::uint32_t g) { return rs[g].gone; }));
  s.also.clear();
  s.tied.open();
  // A lone goal wins: nothing races or ranks against it.
  if (s.goals.size() == 1) {
    stop.check();
    s.work += s.tied.name(s.links, rs[s.goals[0]].link, Kind, s.also);
    TiedInserts::order(s.also);
    return s.goals[0];
  }
  const auto goal = [&](std::size_t k) -> const R& { return rs[s.goals[k]]; };
  // Each goal's root, one paid walk of its path.
  std::vector<std::uint32_t> rootOf(s.goals.size());
  for (std::size_t k = 0; k < s.goals.size(); ++k) {
    std::uint32_t l = goal(k).link;
    for (; s.links[l].parent != kNoEditLink; l = s.links[l].parent) {
      stop.poll();
      ++s.work;
    }
    rootOf[k] = l;
  }
  const auto hash = [&](std::uint32_t l) {
    return static_cast<std::size_t>(Fold(s.links, l));
  };
  const auto same = [&](std::uint32_t a, std::uint32_t b) {
    return Same(s.links, a, b);
  };
  std::unordered_map<std::uint32_t, std::uint32_t, decltype(hash),
                     decltype(same)>
      groups(0, hash, same);
  std::vector<std::uint32_t> of(s.goals.size());
  for (std::size_t k = 0; k < s.goals.size(); ++k) {
    stop.check();
    of[k] = groups
                .try_emplace(goal(k).link,
                             static_cast<std::uint32_t>(groups.size()))
                .first->second;
  }
  std::vector<TextOffset> reach(groups.size(), kRepairReachAll);
  if (groups.size() > 1) race(of, reach);
  const auto before = [&](std::size_t a, std::size_t b) {
    if (reach[of[a]] != reach[of[b]]) return reach[of[a]] > reach[of[b]];
    const auto k = repair_key_cmp(goal(a), goal(b));
    return k != 0 ? k < 0 : s.rank.before(s.links, goal(a).link, goal(b).link);
  };
  std::size_t w = 0;
  for (std::size_t k = 1; k < s.goals.size(); ++k) {
    stop.check();
    if (before(k, w)) w = k;
  }
  for (std::size_t k = 0; k < s.goals.size(); ++k) {
    if (reach[of[k]] != reach[of[w]] || repair_key_cmp(goal(k), goal(w)) != 0 ||
        (roots && rootOf[k] != rootOf[w]))
      continue;
    stop.check();
    s.work += s.tied.name(s.links, goal(k).link, Kind, s.also);
  }
  TiedInserts::order(s.also);
  return s.goals[w];
}

}  // namespace cursiv::engine
