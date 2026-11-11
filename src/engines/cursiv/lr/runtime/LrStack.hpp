// The parse stack: one flat frame per symbol, no graph-structured stack.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "cursiv/utils/macros.hpp"
#include "cursiv/lr/runtime/LrFrame.hpp"

namespace cursiv::engine {

// A reduce drops frames by a subtraction, where `vector::resize` costs a
// divide and a call.
class LrStack {
 public:
  LrStack() = default;
  LrStack(const LrStack&) = delete;
  LrStack& operator=(const LrStack&) = delete;
  ~LrStack() { delete[] _f; }

  void reserve(std::uint32_t n) {
    if (n > capacity()) reallocate(n);
  }
  [[nodiscard]] std::uint32_t size() const {
    return static_cast<std::uint32_t>(_top - _f);
  }

  // Always inlined: with one more caller the inliner outlines it, and every
  // shift and goto then costs a call.
  CURSIV_ALWAYS_INLINE void push(LrFrame frame) {
    if (_top == _end) [[unlikely]]
      grow();
    // Field by field: a whole-frame copy can be lowered to wide integer
    // stores that keep no type, and a store of no type may hit any field of
    // the context, which every later read must then load again.
    LrFrame& f = *_top++;
    f.state = frame.state;
    f.row = frame.row;
    f.open = frame.open;
    f.begin = frame.begin;
    f.word = frame.word;
  }
  void truncate(std::uint32_t n) { assert(n <= size()); _top = _f + n; }

  // Leave the result's slot on top; the caller fills it before reading it.
  CURSIV_ALWAYS_INLINE void reduce(std::uint32_t n) {
    assert(n < size());
    if (n == 0) {
      if (_top == _end) [[unlikely]] grow();
      ++_top;
    } else {
      _top -= n - 1;
    }
  }

  [[nodiscard]] const LrFrame& top() const { assert(_top != _f); return _top[-1]; }
  [[nodiscard]] LrFrame& top() { return under(0); }
  // The frame `k` under the top, at a fixed offset from it. A reduce names the
  // frames it pops: the row a call site writes travels with the frame, so the
  // rename reaches it here.
  [[nodiscard]] LrFrame& under(std::uint32_t k) {
    assert(k < size());
    return _top[-1 - static_cast<std::ptrdiff_t>(k)];
  }
  [[nodiscard]] const LrFrame& operator[](std::uint32_t i) const {
    assert(i < size());
    return _f[i];
  }
  [[nodiscard]] std::uint32_t back() const { return top().state; }
  [[nodiscard]] std::uint32_t state_at(std::uint32_t i) const {
    return (*this)[i].state;
  }

 private:
  static std::uint32_t room_for(std::uint32_t n) {
    constexpr auto max = std::numeric_limits<std::uint32_t>::max();
    return n < 64 ? 64 : (n > max / 2 ? max : n * 2);
  }
  // Out of line, so a push keeps its own inlining.
  [[noreturn]] CURSIV_COLD CURSIV_NOINLINE static void too_deep() {
    throw std::length_error("cursiv: LR stack is too deep");
  }
  // Out of line as well: inlined, the doubling is copied into every goto
  // column, one copy per target state.
  CURSIV_NOINLINE void grow() {
    if (capacity() == std::numeric_limits<std::uint32_t>::max()) [[unlikely]]
      too_deep();
    reallocate(room_for(capacity()));
  }
  CURSIV_NOINLINE void reallocate(std::uint32_t cap) {
    auto* q = new LrFrame[cap];
    const std::uint32_t n = size();
    for (std::uint32_t i = 0; i < n; ++i) q[i] = _f[i];
    delete[] _f;
    _f = q;
    _top = q + n;
    _end = q + cap;
  }
  [[nodiscard]] std::uint32_t capacity() const {
    return static_cast<std::uint32_t>(_end - _f);
  }

  LrFrame* _f = nullptr;
  // One past the top frame, and one past the room.
  LrFrame* _top = nullptr;
  LrFrame* _end = nullptr;
};

}  // namespace cursiv::engine
