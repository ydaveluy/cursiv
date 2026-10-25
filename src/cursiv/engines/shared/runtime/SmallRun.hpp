// A few elements in place, contiguous on the heap past `N`: a walk's usual
// run is one or two, and a frame holding them costs no allocation.
#pragma once

#include <cstddef>
#include <vector>

namespace cursiv {

template <class T, std::size_t N>
class SmallRun {
 public:
  T* begin() { return _heap ? _more.data() : _own; }
  const T* begin() const { return _heap ? _more.data() : _own; }
  T* end() { return begin() + _n; }
  const T* end() const { return begin() + _n; }
  T& operator[](std::size_t i) { return begin()[i]; }
  const T& operator[](std::size_t i) const { return begin()[i]; }
  std::size_t size() const { return _n; }
  bool empty() const { return _n == 0; }
  void push_back(const T& x) {
    if (!_heap && _n < N) {
      _own[_n++] = x;
      return;
    }
    if (!_heap) {
      _more.assign(_own, _own + _n);
      _heap = true;
    }
    _more.push_back(x);
    ++_n;
  }
  void pop_back() {
    --_n;
    if (_heap) _more.pop_back();
  }
  void clear() {
    _n = 0;
    _heap = false;
    _more.clear();
  }

 private:
  T _own[N];
  std::size_t _n = 0;
  bool _heap = false;
  std::vector<T> _more;
};

}  // namespace cursiv
