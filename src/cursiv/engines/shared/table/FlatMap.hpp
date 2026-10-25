// An insert-only map from 64-bit keys, open-addressed, for the generator and
// the repair search. A reference it hands out holds until the next insertion.
// The key `~0` is not for use.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cursiv {

template <class V>
class FlatMap {
 public:
  // The value at `key` and whether it was just made, as `V{}`.
  std::pair<V&, bool> emplace(std::uint64_t key) {
    if (_size * 2 >= _keys.size()) grow();
    const std::size_t mask = _keys.size() - 1;
    for (std::size_t i = mix(key) & mask;; i = (i + 1) & mask) {
      if (_keys[i] == key) return {_vals[i], false};
      if (_keys[i] == kFree) {
        _keys[i] = key;
        ++_size;
        return {_vals[i], true};
      }
    }
  }

  void reset() {
    if (_keys.empty()) return;
    _keys.assign(64, kFree);
    _vals.assign(64, V{});
    _size = 0;
  }

  // The value at `key`, or null.
  V* find(std::uint64_t key) {
    if (_size == 0) return nullptr;
    const std::size_t mask = _keys.size() - 1;
    for (std::size_t i = mix(key) & mask;; i = (i + 1) & mask) {
      if (_keys[i] == key) return &_vals[i];
      if (_keys[i] == kFree) return nullptr;
    }
  }

 private:
  static constexpr std::uint64_t kFree = ~std::uint64_t{0};

  static std::uint64_t mix(std::uint64_t key) {
    std::uint64_t h = 0x9e3779b97f4a7c15ull ^ (key + 0x9e3779b97f4a7c15ull);
    h *= 0xff51afd7ed558ccdull;
    return h ^ (h >> 33);
  }

  void grow() {
    std::vector<std::uint64_t> keys(_keys.empty() ? 64 : _keys.size() * 2,
                                    kFree);
    std::vector<V> vals(keys.size());
    const std::size_t mask = keys.size() - 1;
    for (std::size_t k = 0; k < _keys.size(); ++k) {
      if (_keys[k] == kFree) continue;
      std::size_t i = mix(_keys[k]) & mask;
      while (keys[i] != kFree) i = (i + 1) & mask;
      keys[i] = _keys[k];
      vals[i] = std::move(_vals[k]);
    }
    _keys = std::move(keys);
    _vals = std::move(vals);
  }

  std::vector<std::uint64_t> _keys;
  std::vector<V> _vals;
  std::size_t _size = 0;
};

}  // namespace cursiv
