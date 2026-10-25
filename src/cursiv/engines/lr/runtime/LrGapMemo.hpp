// The answers a condition's reading of the gap gave, by where the gap starts
// and how long it is: the text between two positions never changes, however
// many readings ask. Only a gap longer than a unit's walk is kept, and the
// bytes read to answer one are counted, for a search to pay.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

inline constexpr TextOffset kLrGapKept = 64;

class LrGapMemo {
 public:
  template <class Read>
  bool ask(unsigned leaf, TextOffset from, TextOffset n, Read read) {
    if (n < kLrGapKept) return read();
    if (_slots.empty()) _slots.resize(kSlots);
    Slot& e = _slots[(leaf * 0x9E3779B1u ^ from * 0x85EBCA77u ^ n) % kSlots];
    if (e.leaf == leaf + 1 && e.from == from && e.n == n) return e.answer;
    const bool answer = read();
    _read += n;
    e = Slot{from, n, leaf + 1, answer};
    return answer;
  }
  [[nodiscard]] std::uint64_t read() const { return _read; }
  void clear() { _slots.clear(); }

 private:
  struct Slot {
    TextOffset from = 0;
    TextOffset n = 0;
    unsigned leaf = 0;
    bool answer = false;
  };
  static constexpr std::size_t kSlots = 64;
  std::vector<Slot> _slots;
  std::uint64_t _read = 0;
};

// A reader with no memo reads the gap every time.
template <class Read>
bool lr_gap_ask(LrGapMemo* memo, unsigned leaf, TextOffset from, TextOffset n,
                Read read) {
  return memo != nullptr ? memo->ask(leaf, from, n, read) : read();
}

}  // namespace cursiv
