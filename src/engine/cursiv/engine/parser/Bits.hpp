// A set of small numbers sized at run time: terminals, mostly.
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cursiv::engine::gen {

// Nonempty words of a terminal's rival set, in ascending order.
using RivalRow = std::vector<std::pair<std::uint16_t, std::uint64_t>>;

class Bits {
 public:
  Bits() = default;
  explicit Bits(std::size_t size) : _words((size + 63) / 64, 0) {}

  void set(std::size_t i) { _words[i >> 6] |= std::uint64_t{1} << (i & 63); }
  void reset(std::size_t i) { _words[i >> 6] &= ~(std::uint64_t{1} << (i & 63)); }
  [[nodiscard]] bool test(std::size_t i) const {
    return ((_words[i >> 6] >> (i & 63)) & 1) != 0;
  }
  [[nodiscard]] bool any() const {
    for (const std::uint64_t w : _words)
      if (w != 0) return true;
    return false;
  }
  [[nodiscard]] bool intersects(const Bits& o) const {
    for (std::size_t i = 0; i < _words.size(); ++i)
      if ((_words[i] & o._words[i]) != 0) return true;
    return false;
  }
  void clear() { clear(_words.size()); }
  void clear(std::size_t words) { _words.assign(words, 0); }
  // Whether `a` and `b` hold the same numbers among those of `mask`.
  [[nodiscard]] static bool same_within(const Bits& a, const Bits& b, const Bits& mask) {
    for (std::size_t i = 0; i < mask._words.size(); ++i)
      if (((a._words[i] ^ b._words[i]) & mask._words[i]) != 0) return false;
    return true;
  }
  // True where the union added something. `o` may hold fewer numbers.
  bool merge(const Bits& o) {
    std::uint64_t grew = 0;
    for (std::size_t i = 0; i < o._words.size(); ++i) {
      grew |= o._words[i] & ~_words[i];
      _words[i] |= o._words[i];
    }
    return grew != 0;
  }
  void merge(const RivalRow& row) {
    for (const auto& [i, word] : row) _words[i] |= word;
  }
  [[nodiscard]] bool intersects(const RivalRow& row) const {
    return std::ranges::any_of(row, [&](const auto& entry) {
      return (_words[entry.first] & entry.second) != 0;
    });
  }
  Bits& operator&=(const Bits& o) {
    for (std::size_t i = 0; i < _words.size(); ++i) _words[i] &= o._words[i];
    return *this;
  }
  Bits& remove(const Bits& o) {
    const auto count = std::min(_words.size(), o._words.size());
    for (std::size_t i = 0; i < count; ++i) _words[i] &= ~o._words[i];
    return *this;
  }
  template <class Each>
  void each(Each&& each) const {
    for (std::size_t i = 0; i < _words.size(); ++i)
      for (std::uint64_t w = _words[i]; w != 0; w &= w - 1)
        each(i * 64 + static_cast<std::size_t>(std::countr_zero(w)));
  }
  [[nodiscard]] const std::vector<std::uint64_t>& words() const { return _words; }
  friend bool operator==(const Bits&, const Bits&) = default;
  friend auto operator<=>(const Bits&, const Bits&) = default;

 private:
  std::vector<std::uint64_t> _words;
};

}  // namespace cursiv::engine::gen
