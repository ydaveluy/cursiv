// What walks of the stack's reductions answered, by the frames they read: a
// walk that read the top frames of a stack and asked no condition answers every
// stack that repeats them under the same terminal.
#pragma once

#include <cstdint>
#include <vector>

namespace cursiv {

class LrWalkMemo {
 public:
  static constexpr std::uint32_t kDepth = 8;
  static constexpr std::uint16_t kNone = 0xFFFFu;

  // 0 or 1 where an earlier walk of `term` read frames `top(i)` repeats, -1
  // else; `top(i)` is the state `i` frames from the top, `kNone` past the
  // bottom.
  template <class Top>
  [[nodiscard]] int find(std::uint16_t term, Top&& top) const {
    if (_slots.empty()) return -1;
    const Slot& s = _slots[index(term, top(0), top(1))];
    if (s.depth == 0 || s.term != term) return -1;
    for (std::uint32_t i = 0; i < s.depth; ++i)
      if (top(i) != s.state[i]) return -1;
    return s.verdict;
  }

  template <class Top>
  void put(std::uint16_t term, std::uint32_t depth, bool verdict, Top&& top) {
    if (depth == 0 || depth > kDepth) return;
    if (_slots.empty()) _slots.resize(kSlots);
    Slot& s = _slots[index(term, top(0), top(1))];
    s.term = term;
    s.depth = static_cast<std::uint8_t>(depth);
    s.verdict = verdict ? 1 : 0;
    for (std::uint32_t i = 0; i < depth; ++i)
      s.state[i] = static_cast<std::uint16_t>(top(i));
  }

 private:
  static constexpr std::uint32_t kSlots = 256;
  struct Slot {
    std::uint16_t term = 0;
    std::uint8_t depth = 0;
    std::uint8_t verdict = 0;
    std::uint16_t state[kDepth];
  };
  static std::uint32_t index(std::uint32_t term, std::uint32_t a,
                             std::uint32_t b) {
    return ((term * 0x9E37u + a) * 0x85EBu + b) % kSlots;
  }
  std::vector<Slot> _slots;
};

}  // namespace cursiv
