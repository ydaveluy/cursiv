// Intrusive hash index over caller-owned IDs.
// Each chain step follows one stored link. The buckets grow with the IDs
// added, so a chain stays short whatever the hint given to `init`; a chain's
// order is not part of the contract, and callers compare exactly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cursiv {

struct ChainIndex {
  static constexpr unsigned kNone = 0xffffffffu;
  std::vector<unsigned> head;
  std::vector<unsigned> next;
  std::vector<std::uint64_t> hash;  // by ID, to place it again on a rehash
  std::uint64_t mask = 0;
  std::size_t count = 0;

  void init(std::size_t hint) {
    std::size_t n = 16;
    while (n < hint * 2) n <<= 1;
    head.assign(n, kNone);
    mask = n - 1;
    next.clear();
    hash.clear();
    count = 0;
  }
  unsigned first(std::uint64_t h) const {
    return head[static_cast<std::size_t>((h >> 24) & mask)];
  }
  unsigned after(unsigned i) const { return next[i]; }
  void add(std::uint64_t h, unsigned i) {
    if (++count > head.size()) grow();
    if (next.size() <= i) {
      next.resize(i + 1, kNone);
      hash.resize(i + 1);
    }
    hash[i] = h;
    link(h, i);
  }

 private:
  void link(std::uint64_t h, unsigned i) {
    const auto b = static_cast<std::size_t>((h >> 24) & mask);
    next[i] = head[b];
    head[b] = i;
  }
  // Twice the buckets or more, so that a count of one per bucket is met
  // again only after the count has doubled: the work is linear overall.
  void grow() {
    std::size_t n = head.size();
    while (n < count * 2) n <<= 1;
    std::vector<unsigned> old(n, kNone);
    old.swap(head);
    mask = n - 1;
    for (unsigned b : old)
      for (unsigned i = b, nx; i != kNone; i = nx) {
        nx = next[i];
        link(hash[i], i);
      }
  }
};

}  // namespace cursiv
