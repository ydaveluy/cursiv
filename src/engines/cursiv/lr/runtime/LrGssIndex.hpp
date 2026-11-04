// Immutable graph keys stay in their pools; this index keeps the low bits of
// their hashes and their ids. A matching hash still needs the pool's exact key
// comparison.
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cursiv {

class LrGssIndex {
 public:
  using Id = std::uint32_t;

  void clear() {
    for (Slot& slot : _slots) slot.id = kNone;
    _used = 0;
  }

  // `dead` names the ids no later key can match: a full index sheds them before
  // it grows.
  template <class Match, class Dead>
  Id intern(std::uint64_t hash, Id fresh, Match&& match, Dead&& dead) {
    assert(fresh != kNone);
    if (_slots.empty()) grow(dead);
    const auto low = static_cast<std::uint32_t>(hash);
    std::size_t mask = _slots.size() - 1;
    std::size_t at = low & mask;
    while (_slots[at].id != kNone) {
      if (_slots[at].hash == low && match(_slots[at].id)) return _slots[at].id;
      at = (at + 1) & mask;
    }
    if (2 * (_used + 1) > _slots.size()) {
      grow(dead);
      mask = _slots.size() - 1;
      at = low & mask;
      while (_slots[at].id != kNone) at = (at + 1) & mask;
    }
    _slots[at] = Slot{low, fresh};
    ++_used;
    return fresh;
  }
  template <class Match>
  Id intern(std::uint64_t hash, Id fresh, Match&& match) {
    return intern(hash, fresh, match, [](Id) { return false; });
  }

  [[nodiscard]] std::size_t size() const { return _used; }

 private:
  static constexpr Id kNone = 0xFFFFFFFFu;
  static constexpr std::size_t kShedFrom = std::size_t{1} << 12;
  struct Slot {
    std::uint32_t hash = 0;
    Id id = kNone;
  };

  // Keeps its size only where the live keys fill a quarter of it: shed to just
  // under half, it would rebuild again a few keys later. A table the first
  // cache level holds is cheaper doubled than shed.
  template <class Dead>
  void grow(Dead&& dead) {
    const bool shed = _slots.size() >= kShedFrom;
    std::size_t live = 0;
    for (const Slot& old : _slots)
      if (old.id != kNone && !(shed && dead(old.id))) ++live;
    const std::size_t size = _slots.empty()           ? 16
                             : 4 * (live + 1) > _slots.size() ? 2 * _slots.size()
                                                              : _slots.size();
    std::vector<Slot> next(size);
    const std::size_t mask = next.size() - 1;
    for (const Slot& old : _slots) {
      if (old.id == kNone || (shed && dead(old.id))) continue;
      std::size_t at = old.hash & mask;
      while (next[at].id != kNone) at = (at + 1) & mask;
      next[at] = old;
    }
    _slots.swap(next);
    _used = live;
  }

  std::vector<Slot> _slots;
  std::size_t _used = 0;
};

}  // namespace cursiv
