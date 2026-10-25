// The characters a class names and their UTF-8 byte sequences: the token
// graph holds byte classes only, so a class of scalars is lexed as the
// alternatives of well-formed encodings that cover it. A byte that starts no
// scalar is a character of its own, listed as the range `kStray`; a bound
// refusing the scalar that starts on an earlier byte lists `kBackScalar` plus
// how many bytes earlier.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRanges.hpp"
#include "cursiv/core/text/ClassItem.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"

namespace cursiv {

struct LexCodePoints {
  // One encoding shape: `len` byte ranges, the k-th byte within `lo[k]..hi[k]`.
  struct Seq {
    unsigned char lo[4]{};
    unsigned char hi[4]{};
    unsigned len = 0;
  };
  // Ascending, the markers past the scalars.
  std::vector<unicode::Range> ranges;

  static constexpr char32_t kStray = kNoScalar;
  static constexpr bool marker(const unicode::Range& r) {
    return r.lo > 0x10FFFF;
  }
  bool strays() const { return !ranges.empty() && ranges.back().lo == kStray; }

  // The properties named, one bit each over `unicode::Property`; the scalars
  // a `cr<>` spelling names (`a-z`, `À-ÿ`, `\-`, `\u00e9`, `\s`); the whole
  // complemented among all characters or not. Surrogates are never code
  // points here.
  static LexCodePoints of(std::uint64_t props, bool complement,
                          std::string_view spec) {
    LexCodePoints out;
    for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::count);
         ++i)
      if ((props >> i) & 1)
        out.ranges.insert(out.ranges.end(), unicode::kTables[i].begin(),
                          unicode::kTables[i].end());
    if (!spec.empty()) out.add_spec(spec);
    out.normalize();
    if (complement) out.complement();
    if (out.ranges.empty())
      throw std::invalid_argument("cursiv: a class names no character");
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
  struct StrayLead {
    ByteSet lead;
    unsigned len = 0;
    ByteSet rest[3];
  };
  static std::array<StrayLead, 7> stray_leads() {
    using Span = std::pair<unsigned, unsigned>;
    const auto make = [](std::initializer_list<Span> leads,
                         std::initializer_list<Span> rest) {
      StrayLead out;
      for (const Span& s : leads) out.lead.add(s.first, s.second);
      for (const Span& s : rest) out.rest[out.len++].add(s.first, s.second);
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
    if (strays()) {
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
    for (const Seq& q : sequences())
      if (q.len > 1) out.add(q.lo[0], q.hi[0]);
    for (const unicode::Range& r : ranges) {
      if (r.lo == kStray)
        out.add(0xC2, 0xF4);
      else if (marker(r))
        out.add(0x80, 0xBF);
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
        if (leads.intersects(r.lo == kStray ? starts : continues))
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
    ranges.insert(ranges.end(), o.ranges.begin(), o.ranges.end());
    normalize();
  }

  // Every scalar of `o` is here.
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
          "cursiv: a class is misspelled: a lone or unknown "
          "escape, a class escape at an end of a range, a reversed range, a "
          "surrogate or a value past U+10FFFF, or a byte that is no UTF-8");
  }

  void normalize() {
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
    bool stray = false;
    for (const unicode::Range& r : ranges) {
      if (marker(r)) {
        if (r.lo == kStray)
          stray = true;
        else
          kept.push_back(r);
        continue;
      }
      if (r.lo > next) out.push_back({next, r.lo - 1});
      next = r.hi + 1;
    }
    if (next <= 0x10FFFF) out.push_back({next, 0x10FFFF});
    out.insert(out.end(), kept.begin(), kept.end());
    if (!stray) out.push_back({kStray, kStray});
    ranges = std::move(out);
  }

  // A range becomes the shapes whose bytes vary independently: split at the
  // encoding lengths, around the surrogates, then at every continuation
  // boundary the two ends disagree on.
  static void split(char32_t lo, char32_t hi, std::vector<Seq>& out) {
    std::vector<unicode::Range> work{{lo, hi}};
    while (!work.empty()) {
      auto [a, b] = work.back();
      work.pop_back();
      if (a > b) continue;
      if (a < 0xD800 && b >= 0xD800) {
        work.push_back({0xE000, b});
        b = 0xD7FF;
      } else if (a >= 0xD800 && a <= 0xDFFF) {
        a = 0xE000;
      }
      if (a > b) continue;
      bool cut = false;
      for (const char32_t edge : {char32_t{0x80}, char32_t{0x800},
                                  char32_t{0x10000}})
        if (a < edge && b >= edge) {
          work.push_back({edge, b});
          work.push_back({a, edge - 1});
          cut = true;
          break;
        }
      if (cut) continue;
      std::string la, lb;
      append_scalar(la, a);
      const auto n = static_cast<unsigned>(la.size());
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
      Seq s;
      s.len = n;
      for (unsigned k = 0; k < n; ++k) {
        s.lo[k] = static_cast<unsigned char>(la[k]);
        s.hi[k] = static_cast<unsigned char>(lb[k]);
      }
      out.push_back(s);
    }
  }
};

// The lead bytes of the scalars a row of ranges holds.
inline ByteSet lex_leads_of(const unicode::Range* r, std::size_t n) {
  LexCodePoints cps;
  cps.ranges.assign(r, r + n);
  return cps.leads();
}

}  // namespace cursiv
