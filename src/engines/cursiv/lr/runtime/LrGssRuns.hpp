// Runs of values appended in chunks that never move, each run in one piece: an
// offset names a run for good, and growth copies nothing.
#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace cursiv {

template <class T, unsigned Shift = 14>
class LrGssRuns {
 public:
  // The values appended so far.
  [[nodiscard]] std::size_t size() const { return _appended; }

  // The offset of the run just appended.
  std::size_t append(std::span<const T> run) {
    const std::size_t at = reserve(run.size());
    for (std::size_t i = 0; i != run.size(); ++i) (*this)[at + i] = run[i];
    return at;
  }
  std::size_t append(std::size_t count, const T& value) {
    const std::size_t at = reserve(count);
    for (std::size_t i = 0; i != count; ++i) (*this)[at + i] = value;
    return at;
  }

  T& operator[](std::size_t at) { return _slots[at >> Shift][at & kMask]; }
  const T& operator[](std::size_t at) const {
    return _slots[at >> Shift][at & kMask];
  }
  [[nodiscard]] std::span<const T> span(std::size_t at, std::size_t n) const {
    if (n == 0) return {};
    return std::span<const T>(&(*this)[at], n);
  }

  void clear() {
    _slots.clear();
    _owned.clear();
    _end = 0;
    _appended = 0;
  }

  // The offset of `n` values left for the caller to write, in one piece: the
  // rest of the chunk, or the chunks that follow, made where they are
  // missing. A run past one chunk takes several, side by side.
  std::size_t reserve(std::size_t n) {
    std::size_t at = _end;
    if (n > kChunk || (at & kMask) + n > kChunk) at = (at + kMask) & ~kMask;
    if (n != 0) {
      const std::size_t need = ((at + n - 1) >> Shift) + 1;
      if (_slots.size() < need) {
        const std::size_t fresh = need - _slots.size();
        _owned.push_back(std::make_unique_for_overwrite<T[]>(fresh << Shift));
        T* base = _owned.back().get();
        for (std::size_t i = 0; i != fresh; ++i)
          _slots.push_back(base + (i << Shift));
      }
    }
    assert(at + n <= 0xFFFFFFFFull);
    _end = at + n;
    _appended += n;
    return at;
  }

 private:
  static constexpr std::size_t kChunk = std::size_t{1} << Shift;
  static constexpr std::size_t kMask = kChunk - 1;

  std::vector<T*> _slots;
  std::vector<std::unique_ptr<T[]>> _owned;
  std::size_t _end = 0;
  std::size_t _appended = 0;
};

}  // namespace cursiv
