// A flagged winner a stack refuses, over any graph of frames: the drive, a
// reading, the graph and the caret ask it alike.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/runtime/LrWalkTail.hpp"
#include "cursiv/engines/shared/table/FlatMap.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

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
  bool seenAny = false;
  // The step that last reached each vertex, from 1.
  std::vector<std::uint32_t> reached;
  std::uint32_t step = 0;

  // A step that has reached nothing yet.
  void begin_step() {
    if (++step != 0) return;
    std::ranges::fill(reached, 0u);
    step = 1;
  }
  // Whether `u` is reached for the first time in this step.
  bool reach(Id u) {
    const auto k = static_cast<std::size_t>(u);
    if (k >= reached.size())
      reached.resize(std::max(k + 1, reached.size() * 2), 0u);
    if (reached[k] == step) return false;
    reached[k] = step;
    return true;
  }
};

// Whether one stack below `vertex` takes the token in hand after its plain
// reductions. `act(s, prod, len)` is 1 where state `s` takes it, 0 where it
// refuses it, -1 where it reduces by `prod` of `len` symbols, which lands at
// `target(s, prod)`. A pending reading has one pushed state over a vertex:
// reductions inside that tail stay local, those reaching the graph open a new
// reading, and none uses the machine stack.
template <class Id, class State, class Below, class Act, class Target>
bool lr_graph_takes(LrTakesScratch<Id>& w, Id vertex, State&& state,
                    Below&& below, Act&& act, Target&& target) {
  using Pending = typename LrTakesScratch<Id>::Pending;
  std::vector<Pending>& pending = w.pending;
  pending.assign(1, Pending{vertex, kInvalidSymbol});
  if (w.seenAny) w.seen.reset();
  w.seenAny = false;
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
        above.pop(len);
        const std::uint32_t to =
            target(above.size() != 0 ? above.back() : state(p.vertex), prod);
        if (to == kInvalidSymbol) break;
        above.push(to);
        continue;
      }
      // At each remaining depth a shared vertex is visited once, however
      // many paths of the popped production reach it. In particular a
      // diamond chain costs its graph, not its exponentially many paths.
      layer.assign(1, p.vertex);
      for (std::uint32_t left = len - above.size();
           left != 0 && !layer.empty(); --left) {
        next.clear();
        w.begin_step();
        for (const Id v : layer)
          below(v, [&](Id u) {
            if (w.reach(u)) next.push_back(u);
          });
        layer.swap(next);
      }
      for (auto at = layer.rbegin(); at != layer.rend(); ++at) {
        const std::uint32_t to = target(state(*at), prod);
        if (to != kInvalidSymbol &&
            w.seen.emplace(std::uint64_t{*at} << 32 | to).second) {
          w.seenAny = true;
          pending.push_back({*at, to});
        }
      }
      break;
    }
  }
  return false;
}

// The same, in room of its own.
template <class Id, class State, class Below, class Act, class Target>
bool lr_graph_takes(Id vertex, State&& state, Below&& below, Act&& act,
                    Target&& target) {
  LrTakesScratch<Id> w;
  return lr_graph_takes(w, vertex, state, below, act, target);
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

}  // namespace cursiv
