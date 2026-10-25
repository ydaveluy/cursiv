// The re-readings of a misspelling as the walk emits them: a (terminal, end)
// once, at its fewest faults, laid in rank order when they are asked for --
// fewer faults first, then the longer span, then the walk's own order.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <vector>

#include "cursiv/engines/shared/runtime/TypoWalk.hpp"

namespace cursiv {

class TypoCandidates {
 public:
  void add(const Typo& candidate) {
    const std::uint64_t key =
        (std::uint64_t{candidate.term} << 32) | candidate.end;
    std::size_t at = _found.size();
    if (_indexed.empty() && _found.size() <= kScanned) {
      for (std::size_t k = 0; k < _found.size() && at == _found.size(); ++k)
        if (_found[k].term == candidate.term && _found[k].end == candidate.end)
          at = k;
    } else {
      // A long token has a candidate at every end: found by key from here on.
      if (_indexed.empty())
        for (std::size_t k = 0; k < _found.size(); ++k)
          _indexed.emplace(
              (std::uint64_t{_found[k].term} << 32) | _found[k].end, k);
      if (const auto it = _indexed.find(key); it != _indexed.end())
        at = it->second;
    }
    if (at == _found.size()) {
      if (!_indexed.empty()) _indexed.emplace(key, at);
      _found.push_back(candidate);
      _emitted.push_back(_clock++);
      return;
    }
    if (_found[at].faults <= candidate.faults) return;
    _found[at] = candidate;
    _emitted[at] = _clock++;
  }

  std::vector<Typo> ranked() const {
    std::vector<std::size_t> order(_found.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, [&](std::size_t x, std::size_t y) {
      const Typo& a = _found[x];
      const Typo& b = _found[y];
      if (a.faults != b.faults) return a.faults < b.faults;
      if (a.end != b.end) return a.end > b.end;
      return _emitted[x] < _emitted[y];
    });
    std::vector<Typo> out;
    out.reserve(_found.size());
    for (const std::size_t k : order) out.push_back(_found[k]);
    return out;
  }

 private:
  static constexpr std::size_t kScanned = 32;
  std::vector<Typo> _found;
  std::vector<std::uint64_t> _emitted;
  std::uint64_t _clock = 0;
  std::unordered_map<std::uint64_t, std::size_t> _indexed;
};

}  // namespace cursiv
