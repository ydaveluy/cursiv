// Intrusive hash index over caller-owned IDs.
// Each chain step follows one stored link.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cursiv {

struct ChainIndex {
  static constexpr unsigned kNone = 0xffffffffu;
  std::vector<unsigned> head;
  std::vector<unsigned> next;
  std::uint64_t mask = 0;

  void init(std::size_t hint) {
    std::size_t n = 16;
    while (n < hint * 2) n <<= 1;
    head.assign(n, kNone);
    mask = n - 1;
    next.clear();
  }
  unsigned first(std::uint64_t h) const {
    return head[static_cast<std::size_t>((h >> 24) & mask)];
  }
  unsigned after(unsigned i) const { return next[i]; }
  void add(std::uint64_t h, unsigned i) {
    if (next.size() <= i) next.resize(i + 1, kNone);
    const auto b = static_cast<std::size_t>((h >> 24) & mask);
    next[i] = head[b];
    head[b] = i;
  }
};

}  // namespace cursiv
