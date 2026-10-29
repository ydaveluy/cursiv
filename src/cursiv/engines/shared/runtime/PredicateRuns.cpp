// The free rounds of a condition's repetition, read once per document.
#include "cursiv/engines/shared/runtime/PredicateRuns.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace cursiv {

bool predicate_run(PredicateRuns* runs, std::uint32_t run,
                   const PredicateRound& round, PredicateRest rest,
                   const char* src, TextOffset len, PredicateState cur,
                   TextOffset* seen) {
  // The rounds a lone cursor reached while every round before held one: the
  // cursors visited stand before it, so what follows is what a test from it
  // alone would read. Each is answered by the rounds after it.
  struct Lone {
    TextOffset at;
    TextOffset reach;
    bool holds;
    // No reading held and no gap behind: the round reads as its position.
    bool kept;
  };
  std::vector<Lone> lone;
  // What the rounds after the lone ones answered, together as one set.
  PredicateRunAnswer tail;
  TextOffset* const tailSeen = runs != nullptr ? &tail.reach : seen;
  PredicateState visited = cur, later;
  bool single = runs != nullptr;
  while (!cur.empty()) {
    single = single && cur.size() == 1;
    TextOffset* reach = tailSeen;
    if (single) {
      const PredicateCursor c = cur[0];
      const bool kept = c.held == kUnread && c.from == c.at;
      if (kept)
        if (const PredicateRunAnswer* known =
                runs->find(PredicateRuns::key(run, c.at))) {
          tail = *known;
          break;
        }
      lone.push_back({c.at, 0, false, kept});
      reach = &lone.back().reach;
    }
    PredicateState read, active;
    for (std::size_t i = 0; i < cur.size(); ++i) {
      const PredicateCursor p = round.start(src, len, cur[i], reach);
      (single ? read : later).add(round.between ? cur[i] : p);
      active.add(p);
    }
    if (single) lone.back().holds = rest(src, len, std::move(read), reach);
    const PredicateState next = round.step(src, len, std::move(active), reach);
    cur = next.without(visited);
    visited.unite(cur);
  }
  if (!later.empty())
    tail.holds = rest(src, len, std::move(later), tailSeen) || tail.holds;
  for (auto it = lone.rbegin(); it != lone.rend(); ++it) {
    tail.holds = tail.holds || it->holds;
    tail.reach = std::max(tail.reach, it->reach);
    if (it->kept) runs->keep(PredicateRuns::key(run, it->at), tail);
  }
  if (runs != nullptr && seen != nullptr) *seen = std::max(*seen, tail.reach);
  return tail.holds;
}

}  // namespace cursiv
