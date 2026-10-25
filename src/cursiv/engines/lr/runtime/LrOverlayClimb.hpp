// A stack or a reading's climb seen by a walk: the base stays as it is, and the
// states a reduction pushes ride beside it, so the walk copies and allocates
// nothing. It remembers how deep the walk read into the frames.
#pragma once

#include <algorithm>
#include <cstdint>

#include "cursiv/engines/lr/runtime/LrWalkTail.hpp"

namespace cursiv {

template <class Base>
class LrOverlayClimb {
 public:
  explicit LrOverlayClimb(const Base& b)
      : _base(&b), _kept(b.size()), _start(b.size()), _low(b.size()) {}
  [[nodiscard]] std::uint32_t size() const { return _kept + _tail.size(); }
  [[nodiscard]] std::uint32_t back() const {
    if (_tail.size() != 0) return _tail.back();
    _low = std::min(_low, _kept - 1);
    return _kept == _base->size() ? _base->back() : _base->state_at(_kept - 1);
  }
  [[nodiscard]] std::uint32_t state_at(std::uint32_t i) const {
    if (i >= _kept) return _tail[i - _kept];
    _low = std::min(_low, i);
    return _base->state_at(i);
  }
  void push(std::uint32_t state) { _tail.push(state); }
  void pop(std::uint32_t n) {
    const std::uint32_t fromTail = n < _tail.size() ? n : _tail.size();
    _tail.pop(fromTail);
    _kept -= n - fromTail;
  }

  [[nodiscard]] std::uint32_t peek(std::uint32_t i) const {
    return _base->state_at(i);
  }
  [[nodiscard]] std::uint32_t reach() const { return _start - _low; }
  [[nodiscard]] bool bottom() const { return _low == 0; }

 private:
  const Base* _base;
  std::uint32_t _kept;
  std::uint32_t _start;
  mutable std::uint32_t _low;
  LrWalkTail _tail;
};

}  // namespace cursiv
