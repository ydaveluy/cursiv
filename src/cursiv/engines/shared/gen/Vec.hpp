// The array a lowering and a construction write into: it grows to meet a write
// at any index. A growth moves it: a pointer from `data()` or `row()` dies at
// the next write that extends the array, and so does a reference into it.
#pragma once

#include <cstddef>
#include <vector>

namespace cursiv {

template <class T>
struct Vec {
  std::vector<T> v{};

  // The room doubles, the size meets the write. `resize` alone allocates
  // exactly what is asked, so a row filled one cell at a time would be
  // quadratic -- and every row of a grammar is filled that way.
  T& operator[](std::size_t i) {
    if (i >= v.size()) {
      if (i >= v.capacity()) v.reserve(i < 8 ? 16 : (i + 1) * 2);
      v.resize(i + 1);
    }
    return v[i];
  }
  constexpr const T& operator[](std::size_t i) const { return v[i]; }
  T* row(std::size_t at, std::size_t n) {
    if (at + n > v.size()) {
      if (at + n > v.capacity()) v.reserve((at + n) * 2);
      v.resize(at + n);
    }
    return v.data() + at;
  }
  // What a compaction leaves behind is not data: one table would otherwise
  // compare unequal to itself over cells no reader looks at.
  void truncate(std::size_t k) { v.resize(k); }
  constexpr std::size_t size() const { return v.size(); }
  constexpr const T* data() const { return v.data(); }
};

}  // namespace cursiv
