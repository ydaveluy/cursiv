// The parts of a repair search shared by both engines: its budget, the paths,
// the queue, dominance between readings and the election among the cheapest
// goals. An engine brings its readings and edits.
#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
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
