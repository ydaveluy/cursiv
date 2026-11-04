// A flagged winner a stack refuses, over any graph of frames: the drive, a
// reading, the graph and the caret ask it alike.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "cursiv/lr/runtime/LrWalkTail.hpp"
#include "cursiv/lr/runtime/lr_guard.hpp"
#include "cursiv/utils/RoundMarks.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/table/Token.hpp"

namespace cursiv {

// What `lr_graph_takes` works in, kept by its caller from one call to the next:
// the containers keep their room, and a vertex is marked reached by the step
// that reached it, so no step clears the marks.
template <class Id>
struct LrTakesScratch {
  struct Pending {
    Id vertex;
    std::uint32_t above;
  };
  std::vector<Pending> pending;
  std::vector<Id> layer, next;
  // The landings a reduction already queued, as vertex and state.
  FlatMap<std::uint8_t> seen;
  // The vertices a step has reached.
  RoundMarks reached;
};

// Whether one stack below `vertex` takes the token in hand after its
// reductions. `act(s, prod, len)` is 1 where state `s` takes it, 0 where it
// refuses it, -1 where it reduces by `prod` of `len` symbols, which lands at
// `target(s, prod, first)`, `kInvalidSymbol` for nowhere. `first` is the state
// the span opened on where `opened(prod)` says the landing reads it, and
// `kLrNoFirst` otherwise or for an empty span. A pending reading has one pushed
// state over a vertex: reductions inside that tail stay local, those reaching
// the graph open a new reading, and none uses the machine stack.
template <class Id, class State, class Below, class Act, class Opened,
          class Target>
bool lr_graph_takes(LrTakesScratch<Id>& w, Id vertex, State&& state,
                    Below&& below, Act&& act, Opened&& opened, Target&& target) {
  using Pending = typename LrTakesScratch<Id>::Pending;
  std::vector<Pending>& pending = w.pending;
  pending.assign(1, Pending{vertex, kInvalidSymbol});
  w.seen.reset();
  std::vector<Id>& layer = w.layer;
  std::vector<Id>& next = w.next;
  while (!pending.empty()) {
    const Pending p = pending.back();
    pending.pop_back();
    LrWalkTail above;
    if (p.above != kInvalidSymbol) above.push(p.above);
    for (;;) {
      const std::uint32_t top =
          above.size() != 0 ? above.back() : state(p.vertex);
      std::uint32_t prod = 0, len = 0;
      const int taken = act(top, prod, len);
      if (taken > 0) return true;
      if (taken == 0) break;
      if (len <= above.size()) {
        const std::uint32_t first =
            len != 0 ? above[above.size() - len] : kLrNoFirst;
        above.pop(len);
        const std::uint32_t to = target(
            above.size() != 0 ? above.back() : state(p.vertex), prod, first);
        if (to == kInvalidSymbol) break;
        above.push(to);
        continue;
      }
      const auto land = [&](Id u, std::uint32_t first) {
        const std::uint32_t to = target(state(u), prod, first);
        if (to != kInvalidSymbol &&
            w.seen.emplace(std::uint64_t{u} << 32 | to).second)
          pending.push_back({u, to});
      };
      // At each remaining depth a shared vertex is visited once, however
      // many paths of the popped production reach it. In particular a
      // diamond chain costs its graph, not its exponentially many paths. A
      // landing that reads where its span opened stops one depth short, on
      // those states, and goes down each of their edges.
      const bool reads = opened(prod);
      layer.assign(1, p.vertex);
      for (std::uint32_t left = len - above.size() - (reads ? 1u : 0u);
           left != 0 && !layer.empty(); --left) {
        next.clear();
        w.reached.next();
        for (const Id v : layer)
          below(v, [&](Id u) {
            if (w.reached.first(static_cast<std::size_t>(u))) next.push_back(u);
          });
        layer.swap(next);
      }
      if (reads) {
        for (const Id v : layer)
          below(v, [&](Id u) { land(u, state(v)); });
      } else {
        for (auto at = layer.rbegin(); at != layer.rend(); ++at)
          land(*at, kLrNoFirst);
      }
      break;
    }
  }
  return false;
}

// Whether an arm of a cell of several takes a token, from `state` and through
// the states its empty reductions lead to, each read once: their number is the
// grammar's, not the native stack's. `arms(state, leads)` answers one state,
// telling `leads` each state an empty reduction leads to.
template <class Arms>
bool lr_cell_walk(std::uint32_t state, Arms&& arms) {
  std::vector<std::uint32_t> states;
  std::unordered_set<std::uint32_t> seen;
  const auto leads = [&](std::uint32_t to) {
    if (seen.insert(to).second) states.push_back(to);
  };
  for (std::size_t at = 0;; ++at) {
    if (arms(state, leads)) return true;
    if (at == states.size()) return false;
    state = states[at];
  }
}

// `la` read again by `rescan(la)`, without its terminal, while that terminal
// is flagged and `takes(la)` refuses it. Whether any was refused.
template <class Flagged, class Takes, class Rescan>
inline bool lr_refused(Flagged&& flagged, Takes&& takes, Token& la,
                       Rescan&& rescan) {
  bool refused = false;
  while (flagged(la.sym) && !takes(la)) {
    la = rescan(la);
    refused = true;
  }
  return refused;
}

// What ties with `at`, a token a refusal elected: the sweep answers the keyword
// that was refused and not what it reserved, so each is read in turn by
// `without(at)`, with the ones before it off the position. `ties` says where
// that ends; a token the mode flags stays only if `takes` it. `keep` is told
// each one that stays, and says whether there is room for another.
template <class Without, class Ties, class Flagged, class Takes, class Keep>
inline void lr_refused_ties(Token at, Without&& without, Ties&& ties,
                            Flagged&& flagged, Takes&& takes, Keep&& keep) {
  for (;;) {
    at = without(at);
    if (!ties(at)) return;
    if (flagged(at.sym) && !takes(at)) continue;
    if (!keep(at)) return;
  }
}

}  // namespace cursiv
