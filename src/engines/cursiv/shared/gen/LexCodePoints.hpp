// The characters a class names and their UTF-8 byte sequences, lexed as byte
// classes. A byte that starts no scalar is the range `kInvalid`; a scalar that
// starts on an earlier byte is `kBackScalar` plus how many bytes earlier.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/text/unicode/UnicodeRange.hpp"
#include "cursiv/text/unicode/UnicodeRanges.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/inside_scalar.hpp"
#include "cursiv/text/ClassItem.hpp"
#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

struct LexCodePoints {
  // One encoding shape: `len` byte ranges, the k-th byte within `lo[k]..hi[k]`.
  struct Seq {
    unsigned char lo[4]{};
    unsigned char hi[4]{};
    unsigned len = 0;
  };
  // Ascending, the markers past the scalars.
  std::vector<unicode::Range> ranges;

  static constexpr char32_t kInvalid = kNoScalar;
  static constexpr bool marker(const unicode::Range& r) {
    return r.lo > 0x10FFFF;
  }
  bool holds_invalid() const {
    return !ranges.empty() && ranges.back().lo == kInvalid;
  }

  // The properties named, one bit each over `unicode::Property`, the scalars
  // a `cr<>` spelling names, and whether the whole is complemented.
  static LexCodePoints of(std::uint64_t props, bool complement,
                          std::string_view spec) {
    LexCodePoints out;
    for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::Count);
         ++i)
      if ((props >> i) & 1)
        out.ranges.insert(out.ranges.end(), unicode::kTables[i].begin(),
                          unicode::kTables[i].end());
    if (!spec.empty()) out.add_spec(spec);
    out.normalize();
    if (complement) out.complement();
    if (out.ranges.empty())
      throw std::invalid_argument("a character class matches no character");
    return out;
  }

  // The scalars past ASCII the ranges leave out.
  static LexCodePoints outside(const unicode::Range* r, std::size_t n) {
    LexCodePoints out;
    out.ranges.assign(r, r + n);
    out.ranges.push_back({0, 0x7F});
    out.normalize();
    out.complement();
    std::erase_if(out.ranges,
                  [](const unicode::Range& x) { return marker(x); });
    return out;
  }

  std::vector<Seq> sequences() const {
    std::vector<Seq> out;
    for (const unicode::Range& r : ranges)
      if (!marker(r)) split(r.lo, r.hi, out);
    return out;
  }

  // The lead bytes that start no scalar unless the rest of one follows, each
  // with the classes of that rest.
  struct LoneLead {
    ByteSet lead;
    unsigned len = 0;
    ByteSet rest[3];
  };
  static std::array<LoneLead, 7> lone_leads() {
    using Span = std::pair<unsigned, unsigned>;
    const auto make = [](std::initializer_list<Span> leads,
                         std::initializer_list<Span> rest) {
      LoneLead out;
      for (const auto& [lo, hi] : leads) out.lead.add(lo, hi);
      for (const auto& [lo, hi] : rest) out.rest[out.len++].add(lo, hi);
      return out;
    };
    constexpr Span k{0x80, 0xBF};
    return {make({{0xC2, 0xDF}}, {k}),
            make({{0xE0, 0xE0}}, {{0xA0, 0xBF}, k}),
            make({{0xE1, 0xEC}, {0xEE, 0xEF}}, {k, k}),
            make({{0xED, 0xED}}, {{0x80, 0x9F}, k}),
            make({{0xF0, 0xF0}}, {{0x90, 0xBF}, k, k}),
            make({{0xF1, 0xF3}}, {k, k, k}),
            make({{0xF4, 0xF4}}, {{0x80, 0x8F}, k, k})};
  }

  // The class as a reader keeps it: the bytes it holds outright -- its ASCII
  // and, where it holds the bytes that start no scalar, those that never
  // start one -- and the scalars past them, which a reader decodes for.
  ByteSet bytes() const {
    ByteSet out;
    for (const unicode::Range& r : ranges)
      for (char32_t cp = r.lo; cp < 0x80 && cp <= r.hi; ++cp)
        out.add(static_cast<unsigned char>(cp));
    if (holds_invalid()) {
      out.add(0x80, 0xC1);
      out.add(0xF5, 0xFF);
    }
    return out;
  }
  std::vector<unicode::Range> wide() const {
    std::vector<unicode::Range> out;
    for (const unicode::Range& r : ranges)
      if (r.hi >= 0x80) out.push_back({std::max<char32_t>(r.lo, 0x80), r.hi});
    return out;
  }
  // The lead bytes where a reader decodes: those of its scalars past ASCII,
  // every lead where it holds the bytes that start no scalar, and the
  // continuation bytes where it refuses a scalar begun earlier.
  ByteSet leads() const {
    ByteSet out;
    for (const unicode::Range& r : ranges) {
      if (r.lo == kInvalid) {
        out.add(0xC2, 0xF4);
      } else if (marker(r)) {
        out.add(0x80, 0xBF);
      } else {
        // Within one encoding length the lead rises with the scalar.
        each_length(r.lo, r.hi, [&](char32_t a, char32_t b, unsigned n) {
          if (n > 1) out.add(lead(a, n), lead(b, n));
        });
      }
    }
    return out;
  }

  // Its scalars past ASCII whose encoding opens on one of `leads`; the
  // markers stay where a byte they are read at is one.
  LexCodePoints led(const ByteSet& leads) const {
    LexCodePoints by;
    for (unsigned b = 0xC2; b <= 0xF4; ++b) {
      if (!leads.has(static_cast<unsigned char>(b))) continue;
      const unsigned n = b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
      const unsigned shift = 6 * (n - 1);
      const char32_t lo = static_cast<char32_t>(b & (0x7Fu >> n)) << shift;
      by.ranges.push_back({std::max<char32_t>(lo, n == 2   ? 0x80
                                                  : n == 3 ? 0x800
                                                           : 0x10000),
                           std::min<char32_t>(lo | ((char32_t{1} << shift) - 1),
                                              0x10FFFF)});
    }
    by.normalize();
    LexCodePoints out;
    std::size_t j = 0;
    ByteSet starts, continues;
    starts.add(0xC2, 0xF4);
    continues.add(0x80, 0xBF);
    for (const unicode::Range& r : ranges) {
      if (marker(r)) {
        if (leads.intersects(r.lo == kInvalid ? starts : continues))
          out.ranges.push_back(r);
        continue;
      }
      while (j < by.ranges.size() && by.ranges[j].hi < r.lo) ++j;
      for (std::size_t k = j; k < by.ranges.size() && by.ranges[k].lo <= r.hi;
           ++k)
        out.ranges.push_back({std::max(r.lo, by.ranges[k].lo),
                              std::min(r.hi, by.ranges[k].hi)});
    }
    return out;
  }

  void unite(const LexCodePoints& o) {
    const auto mid = static_cast<std::ptrdiff_t>(ranges.size());
    ranges.insert(ranges.end(), o.ranges.begin(), o.ranges.end());
    std::ranges::inplace_merge(ranges, ranges.begin() + mid, {},
                               &unicode::Range::lo);
    normalize();
  }

  bool covers(const LexCodePoints& o) const {
    LexCodePoints both = *this;
    both.unite(o);
    return both.ranges == ranges;
  }

  // The scalars a word of `n` byte classes spells, where each sequence of
  // bytes it holds is one well-formed scalar; false where one is not.
  static bool spelled(const ByteSet* const* cls, unsigned n,
                      LexCodePoints& out) {
    if (n < 2 || n > 4) return false;
    ByteSet lead, cont;
    lead.add(n == 2 ? 0xC2 : n == 3 ? 0xE0 : 0xF0,
             n == 2 ? 0xDF : n == 3 ? 0xEF : 0xF4);
    cont.add(0x80, 0xBF);
    if (!lead.covers(*cls[0])) return false;
    for (unsigned k = 1; k < n; ++k)
      if (!cont.covers(*cls[k])) return false;
    for (unsigned b = 0; b < 256; ++b) {
      if (!cls[0]->has(static_cast<unsigned char>(b))) continue;
      ByteSet second;
      second.add(b == 0xE0 ? 0xA0 : b == 0xF0 ? 0x90 : 0x80,
                 b == 0xED ? 0x9F : b == 0xF4 ? 0x8F : 0xBF);
      if (!second.covers(*cls[1])) return false;
    }
    out.ranges.clear();
    const auto walk = [&](auto&& self, unsigned k, char32_t v) -> void {
      const ByteSet& s = *cls[k];
      for (unsigned b = 0; b < 256; ++b) {
        if (!s.has(static_cast<unsigned char>(b))) continue;
        if (k == 0) {
          self(self, 1, b & (0x7Fu >> n));
          continue;
        }
        const char32_t at = (v << 6) | (b & 0x3Fu);
        if (k + 1 < n) {
          self(self, k + 1, at);
          continue;
        }
        unsigned e = b;
        while (e + 1 < 256 && s.has(static_cast<unsigned char>(e + 1))) ++e;
        out.ranges.push_back({at, (v << 6) | (e & 0x3Fu)});
        b = e;
      }
    };
    walk(walk, 0, 0);
    out.normalize();
    return !out.ranges.empty();
  }

 private:
  void add_spec(std::string_view spec) {
    const ClassFault fault = for_class_ranges(
        spec, [this](char32_t lo, char32_t hi) { ranges.push_back({lo, hi}); });
    if (fault != ClassFault::none)
      throw std::invalid_argument(
          "a character class is malformed: it has a lone or unknown "
          "escape, a class escape at one end of a range, a reversed range, a "
          "surrogate, a value above U+10FFFF, or a byte that is not valid "
          "UTF-8");
  }

  void normalize() {
    if (!std::ranges::is_sorted(ranges, {}, &unicode::Range::lo))
      std::ranges::sort(ranges, {}, &unicode::Range::lo);
    std::vector<unicode::Range> merged;
    for (const unicode::Range& r : ranges) {
      if (marker(r)) {
        if (merged.empty() || merged.back() != r) merged.push_back(r);
        continue;
      }
      const char32_t hi = std::min<char32_t>(r.hi, 0x10FFFF);
      if (!merged.empty() && !marker(merged.back()) &&
          r.lo <= merged.back().hi + 1)
        merged.back().hi = std::max(merged.back().hi, hi);
      else
        merged.push_back({r.lo, hi});
    }
    ranges = std::move(merged);
  }

  void complement() {
    std::vector<unicode::Range> out, kept;
    char32_t next = 0;
    bool invalid = false;
    for (const unicode::Range& r : ranges) {
      if (marker(r)) {
        if (r.lo == kInvalid)
          invalid = true;
        else
          kept.push_back(r);
        continue;
      }
      if (r.lo > next) out.push_back({next, r.lo - 1});
      next = r.hi + 1;
    }
    if (next <= 0x10FFFF) out.push_back({next, 0x10FFFF});
    out.insert(out.end(), kept.begin(), kept.end());
    if (!invalid) out.push_back({kInvalid, kInvalid});
    ranges = std::move(out);
  }

  // The parts of `lo..hi` one encoding length `n` spells, in order, the
  // surrogates left out: `part(a, b, n)`.
  template <class Part>
  static void each_length(char32_t lo, char32_t hi, Part&& part) {
    struct Length {
      char32_t lo, hi;
      unsigned n;
    };
    static constexpr Length kLengths[] = {{0, 0x7F, 1},
                                          {0x80, 0x7FF, 2},
                                          {0x800, 0xD7FF, 3},
                                          {0xE000, 0xFFFF, 3},
                                          {0x10000, 0x10FFFF, 4}};
    for (const Length& l : kLengths) {
      const char32_t a = std::max(lo, l.lo);
      const char32_t b = std::min(hi, l.hi);
      if (a <= b) part(a, b, l.n);
    }
  }
  // The first byte of scalar `cp` spelled in `n` bytes.
  static unsigned char lead(char32_t cp, unsigned n) {
    return static_cast<unsigned char>(
        n == 1 ? cp : (0xFF00u >> n) | cp >> (6 * (n - 1)));
  }

  // A range becomes the shapes whose bytes vary independently: one per
  // encoding length, then cut at every continuation boundary the two ends
  // disagree on.
  static void split(char32_t lo, char32_t hi, std::vector<Seq>& out) {
    each_length(lo, hi, [&](char32_t from, char32_t to, unsigned n) {
      std::vector<unicode::Range> work{{from, to}};
      while (!work.empty()) {
        const auto [a, b] = work.back();
        work.pop_back();
        std::string la, lb;
        append_scalar(la, a);
        bool cut = false;
        for (unsigned i = 1; i < n && !cut; ++i) {
          const char32_t max = (char32_t{1} << (6 * i)) - 1;
          if ((a & ~max) == (b & ~max)) continue;
          if ((a & max) != 0) {
            work.push_back({(a | max) + 1, b});
            work.push_back({a, a | max});
            cut = true;
          } else if ((b & max) != max) {
            work.push_back({b & ~max, b});
            work.push_back({a, (b & ~max) - 1});
            cut = true;
          }
        }
        if (cut) continue;
        append_scalar(lb, b);
        Seq q;
        q.len = n;
        for (unsigned k = 0; k < n; ++k) {
          q.lo[k] = static_cast<unsigned char>(la[k]);
          q.hi[k] = static_cast<unsigned char>(lb[k]);
        }
        out.push_back(q);
      }
    });
  }
};

}  // namespace cursiv::engine
