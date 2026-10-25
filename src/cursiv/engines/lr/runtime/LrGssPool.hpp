// Elements appended in chunks that never move: a reference stays valid as the
// pool grows, and growth copies nothing, so the peak is the live size. A chunk
// is not written until its elements are, so a short pool costs what it holds.
#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace cursiv {

template <class T, unsigned Shift = 12>
class LrGssPool {
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  [[nodiscard]] std::size_t size() const { return _size; }

  T& operator[](std::size_t i) { return _chunks[i >> Shift][i & kMask]; }
  const T& operator[](std::size_t i) const {
    return _chunks[i >> Shift][i & kMask];
  }

  std::size_t push_back(const T& value) {
    if ((_size >> Shift) == _chunks.size()) {
      std::unique_ptr<T[], Free> chunk(std::allocator<T>().allocate(kChunk));
      _chunks.push_back(std::move(chunk));
    }
    const std::size_t at = _size++;
    (*this)[at] = value;
    return at;
  }

  void clear() {
    _chunks.clear();
    _size = 0;
  }

 private:
  static constexpr std::size_t kChunk = std::size_t{1} << Shift;
  static constexpr std::size_t kMask = kChunk - 1;

  struct Free {
    void operator()(T* chunk) const {
      std::allocator<T>().deallocate(chunk, kChunk);
    }
  };

  std::vector<std::unique_ptr<T[], Free>> _chunks;
  std::size_t _size = 0;
};

}  // namespace cursiv
