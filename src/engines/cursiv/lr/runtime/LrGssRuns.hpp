// Runs of values appended in chunks that never move, each run in one piece: an
// offset names a run for good, and growth copies nothing.
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

template <class T, unsigned Shift = 14>
class LrGssRuns {
 public:
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

  // The offset of `n` values for the caller to write in one piece: the rest of
  // the chunk, or new chunks side by side, published together. A run that would
  // end past 32-bit offsets throws `std::length_error` and makes nothing.
  std::size_t reserve(std::size_t n) {
    std::size_t at = _end;
    if (n > kChunk || (at & kMask) + n > kChunk) at = (at + kMask) & ~kMask;
    if (n > kMostValues || at > kMostValues - n) too_many();
    if (n != 0) {
      const std::size_t need = ((at + n - 1) >> Shift) + 1;
      if (const std::size_t had = _slots.size(); had < need) {
        _owned.push_back(
            std::make_unique_for_overwrite<T[]>((need - had) << Shift));
        _slots.resize(need);
        T* base = _owned.back().get();
        for (std::size_t i = had; i != need; ++i)
          _slots[i] = base + ((i - had) << Shift);
      }
    }
    _end = at + n;
    _appended += n;
    return at;
  }

 private:
  static_assert(Shift > 0 && Shift < 32);
  static constexpr std::size_t kChunk = std::size_t{1} << Shift;
  static constexpr std::size_t kMask = kChunk - 1;
  // The chunks whose offsets stay below 2^32 - 1, and the values they hold.
  static constexpr std::size_t kMostSlots =
      (std::size_t{1} << (32 - Shift)) - 1;
  static constexpr std::size_t kMostValues = kMostSlots << Shift;

  [[noreturn]] CURSIV_COLD CURSIV_NOINLINE static void too_many() {
    throw std::length_error("cursiv: GSS runs exceed their 32-bit offsets");
  }

  std::vector<T*> _slots;
  std::vector<std::unique_ptr<T[]>> _owned;
  std::size_t _end = 0;
  std::size_t _appended = 0;
};

}  // namespace cursiv::engine
