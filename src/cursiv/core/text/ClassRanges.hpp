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
    out.r.reserve(spec.size() / 4 + 1);
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

  // The ranges ascending and joined. They are read in order as a rule, so the
  // sort only runs when the joining pass meets one out of order.
  constexpr void merge() {
    if (joined()) return;
    std::ranges::sort(r, {}, &unicode::Range::lo);
    joined();
  }

  // Joins the ranges in place while each starts at or after the last one
  // kept. At one that starts before, it stops and returns false: the ranges
  // then still hold the same scalars, some of them twice.
  constexpr bool joined() {
    unicode::Range* const first = r.data();
    unicode::Range* w = first;
    for (const unicode::Range* x = first; x != first + r.size(); ++x) {
      if (w != first && x->lo < w[-1].lo) return false;
      w = join(first, w, *x);
    }
    r.resize(static_cast<std::size_t>(w - first));
    return true;
  }

  // Both sides are ascending: one pass takes the lower range next, with no
  // sort. It walks pointers rather than calling the vector, since a constant
  // evaluation counts every call: a union of large classes stays far below
  // the compiler's limit.
  constexpr ClassRanges united(const ClassRanges& o) const {
    ClassRanges out;
    out.strays = strays;
    out.r.resize(r.size() + o.r.size());
    unicode::Range* const first = out.r.data();
    unicode::Range* w = first;
    const unicode::Range* a = r.data();
    const unicode::Range* const aEnd = a + r.size();
    const unicode::Range* b = o.r.data();
    const unicode::Range* const bEnd = b + o.r.size();
    while (a != aEnd && b != bEnd)
      w = join(first, w, a->lo < b->lo ? *a++ : *b++);
    while (a != aEnd) w = join(first, w, *a++);
    while (b != bEnd) w = join(first, w, *b++);
    out.r.resize(static_cast<std::size_t>(w - first));
    return out;
  }

  // Writes `x`, which starts at or after the range before `w`, at `w`, or
  // widens that range where the two touch: the slot after the last range.
  static constexpr unicode::Range* join(const unicode::Range* first,
                                        unicode::Range* w,
                                        const unicode::Range& x) {
    if (w == first || x.lo > w[-1].hi + 1) {
      *w = x;
      return w + 1;
    }
    w[-1].hi = std::max(w[-1].hi, x.hi);
    return w;
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
      if (c >= 0x20 && c < 0x7f && c != '-' && c != '^' && c != '\\') {
        sink(static_cast<char>(c));
        return;
      }
      const unsigned digits = c < 0x100      ? 2
                              : c < 0x10000  ? 4
                              : c < 0x100000 ? 5
                                             : 6;
      sink('\\');
      sink(c < 0x100 ? 'x' : 'u');
      if (digits > 4) sink('{');
      for (unsigned shift = digits * 4; shift != 0; shift -= 4)
        sink("0123456789abcdef"[(c >> (shift - 4)) & 0xF]);
      if (digits > 4) sink('}');
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
