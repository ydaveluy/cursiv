// A flagged winner a stack refuses, over any graph of frames: the drive, a
// reading, the graph and the caret ask it alike.
#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "cursiv/engines/lr/runtime/LrWalkTail.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// Whether one stack below `vertex` takes the token in hand after its plain
// reductions. `act(s, prod, len)` is 1 where state `s` takes it, 0 where it
// refuses it, -1 where it reduces by `prod` of `len` symbols, which lands at
// `target(s, prod)`. A pending reading has one pushed state over a vertex:
// reductions inside that tail stay local, those reaching the graph open a new
// reading, and none uses the machine stack.
template <class Id, class State, class Below, class Act, class Target>
bool lr_graph_takes(Id vertex, State&& state, Below&& below, Act&& act,
                    Target&& target) {
  struct Pending {
    Id vertex;
    std::uint32_t above;
  };
  std::vector<Pending> pending{{vertex, kInvalidSymbol}};
  std::unordered_set<std::uint64_t> seen;
  std::vector<Id> layer, next;
  std::unordered_set<Id> reached;
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
        reached.clear();
        for (const Id v : layer)
          below(v, [&](Id u) {
            if (reached.insert(u).second) next.push_back(u);
          });
        layer.swap(next);
      }
      for (auto at = layer.rbegin(); at != layer.rend(); ++at) {
        const std::uint32_t to = target(state(*at), prod);
        if (to != kInvalidSymbol &&
            seen.insert(std::uint64_t{*at} << 32 | to).second)
          pending.push_back({*at, to});
      }
      break;
    }
  }
  return false;
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
