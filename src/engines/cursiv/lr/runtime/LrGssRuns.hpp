// Runs of values appended in chunks that never move, each run in one piece: an
// offset names a run for good, and growth copies nothing, so the peak is the
// live size. A chunk is not written until its values are. Runs of one value
// take every offset in turn.
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

template <class T, unsigned Shift = 14>
class LrGssRuns {
  static_assert(std::is_trivially_copyable_v<T>);

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
  CURSIV_ALWAYS_INLINE std::size_t reserve(std::size_t n) {
    std::size_t at = _end;
    const bool fits =
        n <= kChunk - (at & kMask) && (at >> Shift) != _slots.size();
    if (!fits) [[unlikely]]
      at = make(n);
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

  // Where a run the last chunk has no room for starts, its chunks made.
  CURSIV_NOINLINE std::size_t make(std::size_t n) {
    std::size_t at = _end;
    if (n > kChunk - (at & kMask)) at = (at + kMask) & ~kMask;
    if (n > kMostValues || at > kMostValues - n) too_many();
    if (n != 0) {
      const std::size_t need = ((at + n - 1) >> Shift) + 1;
      if (const std::size_t had = _slots.size(); had < need) {
        const std::size_t count = (need - had) << Shift;
        _owned.push_back(Block(std::allocator<T>().allocate(count), {count}));
        _slots.resize(need);
        T* base = _owned.back().get();
        for (std::size_t i = had; i != need; ++i)
          _slots[i] = base + ((i - had) << Shift);
      }
    }
    return at;
  }

  struct Free {
    std::size_t count;
    void operator()(T* block) const {
      std::allocator<T>().deallocate(block, count);
    }
  };
  using Block = std::unique_ptr<T[], Free>;

  std::vector<T*> _slots;
  std::vector<Block> _owned;
  std::size_t _end = 0;
  std::size_t _appended = 0;
};

}  // namespace cursiv::engine
