// The parse stack: one flat frame per symbol, no graph-structured stack.
#pragma once

#include <cassert>
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
    if (n > _cap) reallocate(n);
  }
  [[nodiscard]] std::uint32_t size() const { return _n; }

  // Always inlined: with one more caller the inliner outlines it, and every
  // shift and goto then costs a call.
  CURSIV_ALWAYS_INLINE void push(LrFrame frame) {
    if (_n == _cap) [[unlikely]]
      grow();
    // Field by field: a whole-frame copy can be lowered to wide integer
    // stores that keep no type, and a store of no type may hit any field of
    // the context, which every later read must then load again.
    LrFrame& f = _f[_n++];
    f.state = frame.state;
    f.row = frame.row;
    f.open = frame.open;
    f.begin = frame.begin;
    f.word = frame.word;
  }
  void truncate(std::uint32_t n) { assert(n <= _n); _n = n; }

  // Leave the result's slot on top; the caller fills it before reading it.
  CURSIV_ALWAYS_INLINE void reduce(std::uint32_t n) {
    assert(n < _n);
    if (n == 0) {
      if (_n == _cap) [[unlikely]] grow();
      ++_n;
    } else {
      _n -= n - 1;
    }
  }

  [[nodiscard]] const LrFrame& top() const { assert(_n != 0); return _f[_n - 1]; }
  [[nodiscard]] LrFrame& top() { assert(_n != 0); return _f[_n - 1]; }
  [[nodiscard]] const LrFrame& operator[](std::uint32_t i) const {
    assert(i < _n);
    return _f[i];
  }
  // A reduce names the frames it pops: the row a call site writes travels
  // with the frame, so the rename reaches it here.
  [[nodiscard]] LrFrame& operator[](std::uint32_t i) {
    assert(i < _n);
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
    if (_cap == std::numeric_limits<std::uint32_t>::max()) [[unlikely]]
      too_deep();
    reallocate(room_for(_cap));
  }
  CURSIV_NOINLINE void reallocate(std::uint32_t cap) {
    auto* q = new LrFrame[cap];
    for (std::uint32_t i = 0; i < _n; ++i) q[i] = _f[i];
    delete[] _f;
    _f = q;
    _cap = cap;
  }

  LrFrame* _f = nullptr;
  std::uint32_t _n = 0;
  std::uint32_t _cap = 0;
};

}  // namespace cursiv::engine
