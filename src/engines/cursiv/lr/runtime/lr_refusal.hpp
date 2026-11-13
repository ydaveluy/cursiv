// A flagged winner a stack refuses, over any graph of frames: the drive, a
// reading, the graph and the caret ask it the same.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/lr/runtime/LrWalkTail.hpp"
#include "cursiv/lr/runtime/lr_guard.hpp"
#include "cursiv/shared/table/FlatMap.hpp"
#include "cursiv/shared/table/Token.hpp"
#include "cursiv/shared/utils/RoundMarks.hpp"

namespace cursiv::engine {

// What `lr_graph_takes` works in, kept by its caller from one call to the next:
// the containers keep their storage, and a vertex is marked reached by the step
// that reached it, so no step clears the marks.
template <class Id>
struct LrTakesScratch {
  struct Pending {
    Id vertex;
    std::uint32_t above;
  };
  std::vector<Pending> pending;
  std::vector<Id> layer, next;
  // The targets a reduction already queued, as vertex and state.
  FlatMap<std::uint8_t> seen;
  RoundMarks reached;
};

// Whether one stack below `vertex` takes the token in hand after its
// reductions. `act(s, prod, len)` is 1 where `s` takes it, 0 where it refuses,
// -1 where it reduces by `prod`, target at `target(s, prod, first)`.
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
      const auto push_target = [&](Id u, std::uint32_t first) {
        const std::uint32_t to = target(state(u), prod, first);
        if (to != kInvalidSymbol &&
            w.seen.emplace(std::uint64_t{u} << 32 | to).second)
          pending.push_back({u, to});
      };
      // At each remaining depth a shared vertex is visited once, so a diamond
      // chain costs its graph. A target that reads where its span opened stops
      // one depth short and goes down each edge.
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
          below(v, [&](Id u) { push_target(u, state(v)); });
      } else {
        for (auto at = layer.rbegin(); at != layer.rend(); ++at)
          push_target(*at, kLrNoFirst);
      }
      break;
    }
  }
  return false;
}

// Whether an arm of a multi-arm cell takes a token, from `state` and the states
// its empty reductions lead to, each read once. `arms(state, leads)` answers
// one state and tells `leads` where its empty reductions lead.
template <class Arms>
bool lr_cell_walk(std::uint32_t state, Arms&& arms) {
  std::vector<std::uint32_t> states;
  const auto leads = [&](std::uint32_t to) {
    if (std::ranges::find(states, to) == states.end()) states.push_back(to);
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

// What ties with `winner`, which a refusal elected: each terminal `without(at)`
// reads in turn over the same bytes at the same `weight`, a flagged one only if
// `takes` it. `keep` receives each and says whether there is room.
template <class Without, class Weight, class Flagged, class Takes, class Keep>
inline void lr_refused_ties(const Token& winner, std::size_t terms,
                            Without&& without, Weight&& weight,
                            Flagged&& flagged, Takes&& takes, Keep&& keep) {
  const unsigned won = weight(winner);
  for (Token at = winner;;) {
    at = without(at);
    if (at.sym >= terms || at.begin != winner.begin || at.end != winner.end ||
        weight(at) != won)
      return;
    if (flagged(at.sym) && !takes(at)) continue;
    if (!keep(at)) return;
  }
}

}  // namespace cursiv::engine
