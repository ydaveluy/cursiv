// The states a prediction would return into, as the descent pushes and pops
// them. A prediction asks for the whole stack as one context of its pool: the
// frames it has already met are not pushed again, so it costs what changed
// since the last ask and not the depth.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/ll/table/LlSimulation.hpp"

namespace cursiv::engine {

class LlReturnStack {
 public:
  void reserve(std::size_t n) { _frames.reserve(n); }
  void push_back(std::uint32_t ret, std::uint32_t begin = 0) {
    _frames.push_back({ret, begin});
  }
  void pop_back() {
    _frames.pop_back();
    _met = std::min(_met, _frames.size());
  }

  std::size_t size() const { return _frames.size(); }
  std::uint32_t operator[](std::size_t i) const { return _frames[i].ret; }
  std::uint32_t begin(std::size_t i) const { return _frames[i].begin; }

  // The context of the whole stack in `pool`, the one pool a stack is asked
  // for. The nodes it makes stay there for as long as the stack does, which is
  // what lets two asks over one stack answer with one node. `base` is what the
  // first frame returns into.
  std::uint32_t context(LlReturnPool& pool,
                        std::uint32_t base = kLlCtxNone) const {
    if (_nodes.size() <= _frames.size()) _nodes.resize(_frames.size() + 1);
    _nodes[0] = base;
    for (; _met < _frames.size(); ++_met)
      _nodes[_met + 1] =
          pool.push_return(_nodes[_met], pool.return_of(_frames[_met].ret));
    return _nodes[_frames.size()];
  }

 private:
  struct Frame {
    std::uint32_t ret, begin;
  };
  std::vector<Frame> _frames;
  // The context of the first i frames, for i up to `_met`.
  mutable std::vector<std::uint32_t> _nodes;
  mutable std::size_t _met = 0;
};

}  // namespace cursiv::engine
