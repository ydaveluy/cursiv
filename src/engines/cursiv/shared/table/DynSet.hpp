// The bit set construction works in: same surface as `LrBitSet`, this one
// builds, that one is stored. It holds as many words as its highest member
// needs, so a grammar declares as many terminals as it likes.
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace cursiv {

struct DynSet {
  // The last word is never zero: two sets holding the same bits hold the same
  // words, which is what lets a hash and an equality read them alone.
  std::vector<std::uint64_t> w;

  std::uint64_t word(std::size_t k) const { return k < w.size() ? w[k] : 0; }

  // The members, ascending: what a walk costs is how many there are, however
  // far they may range.
  class Members {
   public:
    explicit Members(const DynSet& s) : _s(s) {}
    struct Cursor {
      const std::vector<std::uint64_t>& w;
      std::size_t k = 0;
      std::uint64_t bits = 0;

      void seek() {
        while (bits == 0 && ++k < w.size()) bits = w[k];
      }
      unsigned operator*() const {
        return static_cast<unsigned>(k << 6) +
               static_cast<unsigned>(std::countr_zero(bits));
      }
      Cursor& operator++() {
        bits &= bits - 1;
        seek();
        return *this;
      }
      bool operator!=(std::default_sentinel_t) const { return bits != 0; }
    };
    Cursor begin() const {
      Cursor at{_s.w, 0, _s.w.empty() ? 0 : _s.w[0]};
      at.seek();
      return at;
    }
    std::default_sentinel_t end() const { return {}; }

   private:
    const DynSet& _s;
  };
  Members members() const { return Members(*this); }

  void trim() {
    while (!w.empty() && w.back() == 0) w.pop_back();
  }

  void set(unsigned i) {
    const std::size_t k = i >> 6;
    if (k >= w.size()) w.resize(k + 1, 0);
    w[k] |= std::uint64_t{1} << (i & 63);
  }
  void reset(unsigned i) {
    const std::size_t k = i >> 6;
    if (k >= w.size()) return;
    w[k] &= ~(std::uint64_t{1} << (i & 63));
    trim();
  }
  // How many members stand below `i`: the index of `i` in a row laid in
  // bit order.
  std::uint32_t rank(unsigned i) const {
    std::uint32_t r = 0;
    const std::size_t n = std::min<std::size_t>(w.size(), i / 64);
    for (std::size_t k = 0; k < n; ++k)
      r += static_cast<std::uint32_t>(std::popcount(w[k]));
    return r + static_cast<std::uint32_t>(std::popcount(
                   word(i / 64) & ((std::uint64_t{1} << (i % 64)) - 1)));
  }
  bool test(unsigned i) const {
    const std::size_t k = i >> 6;
    return k < w.size() && ((w[k] >> (i & 63)) & 1) != 0;
  }

  bool any() const { return !w.empty(); }
  bool none() const { return w.empty(); }

  DynSet& operator|=(const DynSet& o) {
    add(o);
    return *this;
  }
  DynSet& operator&=(const DynSet& o) {
    for (std::size_t k = 0; k < w.size(); ++k) w[k] &= o.word(k);
    trim();
    return *this;
  }
  DynSet& andnot(const DynSet& o) {
    for (std::size_t k = 0; k < w.size(); ++k) w[k] &= ~o.word(k);
    trim();
    return *this;
  }

  friend DynSet operator|(DynSet a, const DynSet& b) {
    a |= b;
    return a;
  }
  friend DynSet operator&(DynSet a, const DynSet& b) {
    a &= b;
    return a;
  }
  friend bool operator==(const DynSet&, const DynSet&) = default;

  bool intersects(const DynSet& o) const {
    const std::size_t m = std::min(w.size(), o.w.size());
    for (std::size_t k = 0; k < m; ++k)
      if (w[k] & o.w[k]) return true;
    return false;
  }
  bool covers(const DynSet& o) const {
    if (o.w.size() > w.size()) return false;
    for (std::size_t k = 0; k < o.w.size(); ++k)
      if ((w[k] | o.w[k]) != w[k]) return false;
    return true;
  }
  bool add(const DynSet& o) {
    if (o.w.size() > w.size()) w.resize(o.w.size(), 0);
    bool grew = false;
    for (std::size_t k = 0; k < o.w.size(); ++k) {
      const std::uint64_t v = w[k] | o.w[k];
      if (v != w[k]) {
        w[k] = v;
        grew = true;
      }
    }
    return grew;
  }

  bool more_than_one() const {
    bool one = false;
    for (const std::uint64_t x : w) {
      if (!x) continue;
      if (one || (x & (x - 1))) return true;
      one = true;
    }
    return false;
  }
};

}  // namespace cursiv
