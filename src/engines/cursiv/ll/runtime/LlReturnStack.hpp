// The states a prediction would return into, as the descent pushes and pops
// them. A frame keeps its node in the pool once asked, so an ask costs what
// changed, not the depth.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/ll/table/LlSimulation.hpp"

namespace cursiv::engine {

class LlReturnStack {
 public:
  void reserve(std::size_t n) { _frames.reserve(n); }
  void push_back(std::uint32_t ret, std::uint32_t begin = 0) {
    _frames.push_back({ret, begin, kLlCtxNone});
  }
  void pop_back() { _frames.pop_back(); }

  std::size_t size() const { return _frames.size(); }
  std::uint32_t operator[](std::size_t i) const { return _frames[i].ret; }
  std::uint32_t begin(std::size_t i) const { return _frames[i].begin; }

  // The context of the whole stack in `pool`, the only pool it is asked for.
  // `base` is what the first frame returns into.
  std::uint32_t context(LlReturnPool& pool,
                        std::uint32_t base = kLlCtxNone) const {
    std::size_t known = _frames.size();
    while (known != 0 && _frames[known - 1].node == kLlCtxNone) --known;
    std::uint32_t node = known != 0 ? _frames[known - 1].node : base;
    for (; known < _frames.size(); ++known)
      node = _frames[known].node =
          pool.push_return(node, pool.return_of(_frames[known].ret));
    return node;
  }

 private:
  struct Frame {
    std::uint32_t ret, begin;
    // The context of the frames up to this one, once asked.
    mutable std::uint32_t node;
  };
  std::vector<Frame> _frames;
};

}  // namespace cursiv::engine
