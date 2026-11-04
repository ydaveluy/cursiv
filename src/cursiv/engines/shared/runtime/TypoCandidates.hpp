// The re-readings of a misspelling as the walk emits them: a (terminal, end)
// once, at its least cost, laid in rank order when they are asked for -- the
// cheaper first, then the longer span, then the walk's own order.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/runtime/TypoWalk.hpp"
#include "cursiv/engines/shared/table/FlatMap.hpp"

namespace cursiv {

class TypoCandidates {
 public:
  // Out of line: inlined into the walk, it costs the walk the inlining of its
  // own table calls, which a search pays byte by byte.
  CURSIV_NOINLINE void add(const Typo& candidate) {
    // Never `~0`, which the map keeps: a terminal is 16 bits of the high word.
    const auto [at, fresh] =
        _at.emplace((std::uint64_t{candidate.term} << 32) | candidate.end);
    if (fresh) {
      at = _found.size();
      _found.push_back(candidate);
      _emitted.push_back(_clock++);
      return;
    }
    if (_found[at].cost <= candidate.cost) return;
    _found[at] = candidate;
    _emitted[at] = _clock++;
  }

  std::vector<Typo> ranked() const {
    std::vector<std::size_t> order(_found.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, [&](std::size_t x, std::size_t y) {
      const Typo& a = _found[x];
      const Typo& b = _found[y];
      if (a.cost != b.cost) return a.cost < b.cost;
      if (a.end != b.end) return a.end > b.end;
      return _emitted[x] < _emitted[y];
    });
    std::vector<Typo> out;
    out.reserve(_found.size());
    for (const std::size_t k : order) out.push_back(_found[k]);
    return out;
  }

 private:
  std::vector<Typo> _found;
  std::vector<std::uint64_t> _emitted;
  std::uint64_t _clock = 0;
  FlatMap<std::size_t> _at;
};

}  // namespace cursiv
