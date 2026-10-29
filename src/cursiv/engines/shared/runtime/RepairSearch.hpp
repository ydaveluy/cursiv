// What a repair search is whatever its engine: the work a parse and each of
// its searches may spend, the paths the readings take, the queue that orders
// them, a reading's dominance by another of its future, and the election among
// the goals of the least cost. An engine brings its readings and gestures.
#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/GestureRank.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "cursiv/engines/shared/runtime/TiedInserts.hpp"

namespace cursiv {

// The reach of a goal that holds, or whose reading no refusal stopped.
inline constexpr TextOffset kRepairReachAll = 0xFFFFFFFFu;

// A reading queued at `f`, with the rest of its key and the order it was
// queued in.
struct RepairEntry {
  std::uint32_t f, lost, faults, invented, seq, at;
};

// Two readings by key: cost, then the author's tokens lost, the faults, the
// tokens invented.
template <class R>
std::strong_ordering repair_key_cmp(const R& a, const R& b) {
  const std::uint64_t ka = (std::uint64_t{a.cost} << 32) | a.lost;
  const std::uint64_t kb = (std::uint64_t{b.cost} << 32) | b.lost;
  if (ka != kb) return ka <=> kb;
  return ((std::uint64_t{a.faults} << 32) | a.invented) <=>
         ((std::uint64_t{b.faults} << 32) | b.invented);
}

// The queue hands out the least key first, then the first queued.
inline constexpr auto repair_entry_after = [](const RepairEntry& a,
                                              const RepairEntry& b) {
  const std::uint64_t ka = (std::uint64_t{a.f} << 32) | a.lost;
  const std::uint64_t kb = (std::uint64_t{b.f} << 32) | b.lost;
  if (ka != kb) return ka > kb;
  const std::uint64_t la = (std::uint64_t{a.faults} << 32) | a.invented;
  const std::uint64_t lb = (std::uint64_t{b.faults} << 32) | b.invented;
  if (la != lb) return la > lb;
  return a.seq > b.seq;
};

// Roots precede every gesture, so their link ids are source ranks, also
// after an admission redirects a path through an earlier source.
template <class Gesture>
std::uint32_t repair_root_of(const std::vector<Gesture>& links,
                             std::uint32_t l) {
  while (links[l].parent != kNoGestureLink) l = links[l].parent;
  return l;
}

template <class Gesture>
struct RepairSearch {
  RepairSearch(TextOffset len, const RepairBudget& b)
      : budget(repair_allot(b, len)), perSearch(b.perSearch) {}

  std::uint64_t budget;
  std::uint64_t perSearch;
  std::uint64_t work = 0;
  // Where the search in flight stops: its share of what the parse has left.
  std::uint64_t limit = 0;
  // The parse's budget answered: every later fault is repaired by the first
  // gesture that holds.
  bool degraded = false;
  std::vector<Gesture> links;
  // Links a dominated reading handed to the one that kept its future.
  std::uint64_t handed = 0;
  GestureRank rank;
  std::vector<RepairEntry> heap;
  std::uint32_t seq = 0;
  // The goals of the least cost.
  std::vector<std::uint32_t> goals;
  // What the elected fault's first insertion could as cheaply have been, and
  // the paths dropped for one as cheap.
  std::vector<ElementId> also;
  TiedInserts tied;

  std::uint32_t link(const Gesture& g) {
    links.push_back(g);
    return static_cast<std::uint32_t>(links.size()) - 1;
  }

  // Reading `rs[at]`, as `r` stands, queued at `f` with `lost` more tokens
  // lost. One that took a token since its last gesture opens a fault with the
  // next one: `opens`.
  template <class R>
  void queue(std::vector<R>& rs, std::uint32_t f, const R& r, std::uint32_t at,
             std::uint32_t lost, bool opens) {
    heap.push_back(RepairEntry{f, r.lost + lost, r.faults + (opens ? 1u : 0u),
                               r.invented, seq++, at});
    std::push_heap(heap.begin(), heap.end(), repair_entry_after);
    rs[at].queued = true;
  }

  RepairEntry pop() {
    std::pop_heap(heap.begin(), heap.end(), repair_entry_after);
    const RepairEntry e = heap.back();
    heap.pop_back();
    return e;
  }

  // A search set out afresh, at its share of what the parse has left.
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

// Reading `rs[i]` meets the one its future keeps, `rs[kept]`. It dies to it at
// a worse key and no more of allowance `A`, the insertions an owed run still
// makes free, or at an equal key and allowance, where the kept one takes the
// better rank's path: what descends from it follows, and what the dropped path
// first inserted, as `kind` reads a link, stays named. Two at an equal key and
// different allowances both stand, as two that neither outdoes do; one that
// outdoes the kept one is kept instead. Whether it dies.
template <auto A, class G, class R, class Kind>
CURSIV_ALWAYS_INLINE inline bool repair_meet(RepairSearch<G>& s,
                                             std::vector<R>& rs,
                                             std::uint32_t i,
                                             std::uint32_t& kept, Kind&& kind) {
  R& r = rs[i];
  R& x = rs[kept];
  const auto k = x.gone ? std::strong_ordering::less : repair_key_cmp(r, x);
  if (k > 0 ? r.*A <= x.*A : k == 0 && r.*A == x.*A) {
    if (k == 0) {
      const GestureRank::Parting p = s.rank.part(s.links, r.link, x.link);
      const bool handed = r.link != x.link && GestureRank::before(s.links, p);
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

// The furthest reach, then the key, then the rank. Goals whose paths make the
// same gestures, as `Fold` and `Same` tell, race as one, as far as the
// furthest of them goes: `race(of, reach)`. What the elected path's first
// insertion could as cheaply have been is named from every goal it ties with
// -- where `roots`, at its own root.
template <auto Fold, auto Same, auto Kind, class G, class R, class Race>
std::uint32_t repair_elect(RepairSearch<G>& s, const std::vector<R>& rs,
                           bool roots, Race&& race) {
  std::erase_if(s.goals, [&](std::uint32_t g) { return rs[g].gone; });
  const auto goal = [&](std::size_t k) -> const R& { return rs[s.goals[k]]; };
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
  for (std::size_t k = 0; k < s.goals.size(); ++k)
    of[k] = groups
                .try_emplace(goal(k).link,
                             static_cast<std::uint32_t>(groups.size()))
                .first->second;
  std::vector<TextOffset> reach(groups.size(), kRepairReachAll);
  if (groups.size() > 1) race(of, reach);
  const auto before = [&](std::size_t a, std::size_t b) {
    if (reach[of[a]] != reach[of[b]]) return reach[of[a]] > reach[of[b]];
    const auto k = repair_key_cmp(goal(a), goal(b));
    return k != 0 ? k < 0 : s.rank.before(s.links, goal(a).link, goal(b).link);
  };
  std::size_t w = 0;
  for (std::size_t k = 1; k < s.goals.size(); ++k)
    if (before(k, w)) w = k;
  const std::uint32_t root = roots ? repair_root_of(s.links, goal(w).link) : 0;
  s.also.clear();
  s.tied.open();
  for (std::size_t k = 0; k < s.goals.size(); ++k) {
    if (reach[of[k]] != reach[of[w]] || repair_key_cmp(goal(k), goal(w)) != 0 ||
        (roots && repair_root_of(s.links, goal(k).link) != root))
      continue;
    s.tied.name(s.links, goal(k).link, Kind, s.also);
  }
  TiedInserts::order(s.also);
  return s.goals[w];
}

}  // namespace cursiv
