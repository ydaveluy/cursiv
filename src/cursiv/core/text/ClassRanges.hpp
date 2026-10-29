// The scalars a class spelling names, as ascending maximal ranges: the
// compile-time algebra behind `cr<>`, `|` and `~`, and the one spelling every
// class is keyed on. `strays` is set where the spelling holds a complement
// escape (`\D`, `\W`, `\S`), which also holds the bytes that start no scalar.
// Transient: it never leaves a constant evaluation.
#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/text/ClassItem.hpp"

namespace cursiv {

struct ClassRanges {
  std::vector<unicode::Range> r;
  bool strays = false;

  static constexpr ClassRanges of(std::string_view spec) {
    ClassRanges out;
    for (std::size_t i = 0; i < spec.size();) {
      const ClassItem it = class_item_at(spec, i);
      if (it.fault != ClassFault::none) break;
      if (it.klass != 0) {
        out.strays = out.strays || (it.klass & 0x20) == 0;
        class_escape_ranges(it.klass, [&out](char32_t lo, char32_t hi) {
          out.r.push_back({lo, hi});
        });
      } else {
        out.r.push_back({it.lo, it.hi});
      }
      i = it.end;
    }
    out.merge();
    return out;
  }

  constexpr void merge() {
    const auto before = [](const unicode::Range& a, const unicode::Range& b) {
      return a.lo < b.lo;
    };
    if (!std::ranges::is_sorted(r, before)) std::ranges::sort(r, before);
    std::vector<unicode::Range> out;
    out.reserve(r.size());
    for (const unicode::Range& x : r) {
      if (!out.empty() && x.lo <= out.back().hi + 1)
        out.back().hi = std::max(out.back().hi, x.hi);
      else
        out.push_back(x);
    }
    r = std::move(out);
  }

  constexpr ClassRanges united(const ClassRanges& o) const {
    ClassRanges out = *this;
    out.r.insert(out.r.end(), o.r.begin(), o.r.end());
    out.merge();
    return out;
  }

  constexpr ClassRanges complemented() const {
    ClassRanges out;
    out.r.reserve(r.size() + 1);
    char32_t next = 0;
    for (const unicode::Range& x : r) {
      if (x.lo > next) out.r.push_back({next, x.lo - 1});
      next = x.hi + 1;
    }
    if (next <= 0x10FFFF) out.r.push_back({next, 0x10FFFF});
    return out;
  }

  constexpr ClassRanges intersected(const ClassRanges& o) const {
    return complemented().united(o.complemented()).complemented();
  }

  constexpr ClassRanges minus(const ClassRanges& o) const {
    ClassRanges out;
    out.r.reserve(r.size());
    std::size_t i = 0;
    for (const unicode::Range& x : r) {
      char32_t next = x.lo;
      while (i < o.r.size() && o.r[i].hi < next) ++i;
      while (i < o.r.size() && o.r[i].lo <= x.hi) {
        const unicode::Range& cut = o.r[i];
        if (cut.lo > next) out.r.push_back({next, cut.lo - 1});
        if (cut.hi >= x.hi) {
          next = x.hi + 1;
          break;
        }
        next = cut.hi + 1;
        ++i;
      }
      if (next <= x.hi) out.r.push_back({next, x.hi});
    }
    return out;
  }

  // The canonical spelling: bare where printable and not syntax, `\xHH`,
  // `\uHHHH` or `\u{H…}` otherwise, so no spelling reads two ways.
  template <class Sink>
  constexpr void spell(Sink sink) const {
    const auto put = [&sink](char32_t c) {
      constexpr char kHex[] = "0123456789abcdef";
      if (c >= 0x20 && c < 0x7f && c != '-' && c != '^' && c != '\\') {
        sink(static_cast<char>(c));
        return;
      }
      sink('\\');
      const std::size_t digits = c < 0x100 ? 2 : c < 0x10000 ? 4 : 0;
      sink(c < 0x100 ? 'x' : 'u');
      if (digits == 0) sink('{');
      std::size_t shift = 0;
      for (char32_t v = c; v != 0; v >>= 4) shift += 4;
      if (shift == 0) shift = 4;
      if (digits != 0 && shift < digits * 4) shift = digits * 4;
      for (; shift != 0; shift -= 4) sink(kHex[(c >> (shift - 4)) & 0xF]);
      if (digits == 0) sink('}');
    };
    for (const unicode::Range& x : r) {
      put(x.lo);
      if (x.hi == x.lo + 1) {
        put(x.hi);
      } else if (x.hi > x.lo) {
        sink('-');
        put(x.hi);
      }
    }
  }

  constexpr std::size_t spelled_size() const {
    std::size_t n = 0;
    spell([&n](char) { ++n; });
    return n;
  }
};

}  // namespace cursiv
