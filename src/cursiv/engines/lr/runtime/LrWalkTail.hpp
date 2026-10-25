// The states a walk pushes over the frames it stands on: a few cells inline,
// the rest spilled, so a walk keeps no bound and a short one allocates nothing.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv {

class LrWalkTail {
 public:
  [[nodiscard]] std::uint32_t size() const { return _n; }
  [[nodiscard]] std::uint32_t operator[](std::uint32_t i) const {
    return i < kRoom ? _cells[i] : _spill[i - kRoom];
  }
  [[nodiscard]] std::uint32_t back() const { return (*this)[_n - 1]; }
  void push(std::uint32_t state) {
    if (_n < kRoom)
      _cells[_n] = state;
    else
      _spill.push_back(state);
    ++_n;
  }
  void pop(std::uint32_t k) {
    _n -= k;
    if (_n + k > kRoom) _spill.resize(_n > kRoom ? _n - kRoom : 0);
  }

 private:
  static constexpr std::uint32_t kRoom = 16;
  std::uint32_t _n = 0;
  std::uint32_t _cells[kRoom];
  std::vector<std::uint32_t> _spill;
};

}  // namespace cursiv
