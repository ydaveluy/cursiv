// A reading's stack over the cactus: the climb `lr_apply_reduce` pops and
// pushes, and the tokens the reading has shifted, which a pushed frame
// begins at -- a reduce's frame at the first frame it pops.
#pragma once

#include <cstdint>

#include "cursiv/engines/lr/runtime/LrCactus.hpp"

namespace cursiv {

class LrCactusClimb {
 public:
  LrCactusClimb() = default;
  LrCactusClimb(LrCactus& pool, const LrCactus::Top& from)
      : _pool(&pool),
        _top(from.node),
        _begin(from.begin),
        _depth(from.depth),
        _low(from.depth) {}

  [[nodiscard]] std::uint32_t size() const { return _depth; }
  [[nodiscard]] std::uint32_t back() const { return _pool->state(_top); }
  [[nodiscard]] std::uint32_t state_at(std::uint32_t i) const {
    std::uint32_t n = _top;
    for (std::uint32_t d = _depth - 1; d > i; --d) n = _pool->parent(n);
    return _pool->state(n);
  }
  void push(std::uint32_t state) {
    _top = _pool->intern(static_cast<std::uint16_t>(state), _top);
    _begin = _pool->begin_push(_go ? _goBegin : _tokens, _begin);
    _go = false;
    ++_depth;
  }
  void pop(std::uint32_t n) {
    _go = true;
    _goBegin = _tokens;
    for (std::uint32_t k = 0; k < n; ++k) {
      _goBegin = _pool->begin_of(_begin);
      _top = _pool->parent(_top);
      _begin = _pool->begin_parent(_begin);
    }
    _depth -= n;
    if (_depth < _low) _low = _depth;
  }

  // Frames given up, not reduced: nothing is pushed for them.
  void truncate(std::uint32_t n) {
    pop(n);
    _go = false;
  }

  [[nodiscard]] std::uint32_t top() const { return _top; }
  [[nodiscard]] std::uint32_t begin_node() const { return _begin; }
  // The shallowest this reading stood since the search set out: below it,
  // its frames are the frozen stack's.
  [[nodiscard]] std::uint32_t low() const { return _low; }
  [[nodiscard]] std::int64_t tokens() const { return _tokens; }
  void add_token() { ++_tokens; }

 private:
  LrCactus* _pool = nullptr;
  std::uint32_t _top = kLrNoNode;
  std::uint32_t _begin = kLrNoNode;
  std::uint32_t _depth = 0;
  std::uint32_t _low = 0;
  std::int64_t _tokens = 0;
  std::int64_t _goBegin = 0;
  bool _go = false;
};

}  // namespace cursiv
