// The re-readings of a misspelling are kept and ranked the same way for every
// language.
#include "cursiv/shared/runtime/TypoCandidates.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace cursiv::engine {

void TypoCandidates::add(const Typo& candidate) {
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

std::vector<Typo> TypoCandidates::ranked() const {
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

}  // namespace cursiv::engine
