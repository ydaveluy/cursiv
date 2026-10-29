// The terminals a state reads, with the alphabet its position reads them under,
// and the states that read a position, taken once per table off the graph: a
// repair asks it of every reading at every token, and two readings on one
// state ask the same.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/table/LlAtn.hpp"
#include "cursiv/engines/shared/table/Token.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

template <const auto& T>
class LlAtoms {
 public:
  struct Atom {
    std::uint16_t label;
    std::uint16_t mode;
  };

  LlAtoms() : _base(T.atn.nstates + 1, 0) {
    for (std::uint32_t s = 0; s < T.atn.nstates; ++s) {
      const LlAtnState& state = T.atn.state(s);
      for (std::uint32_t i = 0; i < state.nedges; ++i) {
        const LlAtnEdge& e = T.atn.edge(state.edge + i);
        if (e.kind != LlEdgeKind::atom && e.kind != LlEdgeKind::condition)
          continue;
        const std::uint16_t hop = T.edgeNode[state.edge + i];
        if (hop != kNoRow) _reads.emplace_back(hop, s);
        if (e.kind != LlEdgeKind::atom) continue;
        if (e.label == kEofSymbol) continue;
        _atoms.push_back(
            Atom{e.label, hop != kNoRow ? T.nodes[hop].mode : T.errMode});
      }
      _base[s + 1] = static_cast<std::uint32_t>(_atoms.size());
    }
    std::ranges::sort(_reads);
  }

  std::span<const Atom> at(std::uint32_t state) const {
    return {_atoms.data() + _base[state], _atoms.data() + _base[state + 1]};
  }

  // The states that read position `node`, which is where a token it expects
  // is missing.
  std::span<const std::pair<std::uint16_t, std::uint32_t>> reading(
      std::uint16_t node) const {
    const auto lo = std::ranges::lower_bound(
        _reads, std::pair<std::uint16_t, std::uint32_t>{node, 0});
    auto hi = lo;
    while (hi != _reads.end() && hi->first == node) ++hi;
    return {_reads.data() + (lo - _reads.begin()),
            static_cast<std::size_t>(hi - lo)};
  }

 private:
  std::vector<std::uint32_t> _base;
  std::vector<Atom> _atoms;
  std::vector<std::pair<std::uint16_t, std::uint32_t>> _reads;
};

template <const auto& T>
inline const LlAtoms<T>& ll_atoms() {
  static const LlAtoms<T> atoms;
  return atoms;
}

}  // namespace cursiv
