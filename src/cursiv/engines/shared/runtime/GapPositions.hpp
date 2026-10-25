// The positions a gap's expression reaches, one bit each: a word while the
// gap is shorter than 64 bytes, a span of words past it. Both step through
// `gap_repeat` and answer through `gap_matches`.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/text/scalar_at.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct GapPositions {
  struct Word {
    std::uint64_t w = 0;
    bool empty() const { return w == 0; }
    bool has(TextOffset p) const { return ((w >> p) & 1u) != 0; }
    void set(TextOffset p) { w |= std::uint64_t{1} << p; }
    template <class F>
    void each(F&& f) const {
      for (auto b = w; b != 0; b &= b - 1)
        f(static_cast<TextOffset>(std::countr_zero(b)));
    }
    void unite(const Word& o) { w |= o.w; }
    Word without(const Word& o) const { return {w & ~o.w}; }
    bool operator==(const Word&) const = default;
  };

  struct Span {
    std::size_t base = 0;
    std::vector<std::uint64_t> w;
    bool empty() const { return w.empty(); }
    bool has(TextOffset p) const {
      const std::size_t i = p >> 6;
      return i >= base && i - base < w.size() &&
             ((w[i - base] >> (p & 63u)) & 1u) != 0;
    }
    void set(TextOffset p) {
      const std::size_t i = p >> 6;
      if (w.empty())
        base = i;
      else if (i < base) {
        w.insert(w.begin(), base - i, 0);
        base = i;
      }
      if (i - base >= w.size()) w.resize(i - base + 1u, 0);
      w[i - base] |= std::uint64_t{1} << (p & 63u);
    }
    template <class F>
    void each(F&& f) const {
      for (std::size_t i = 0; i < w.size(); ++i)
        for (auto b = w[i]; b != 0; b &= b - 1)
          f(static_cast<TextOffset>((base + i) * 64u + std::countr_zero(b)));
    }
    void unite(const Span& o) {
      o.each([&](TextOffset p) { set(p); });
    }
    Span without(const Span& o) const {
      Span r;
      each([&](TextOffset p) {
        if (!o.has(p)) r.set(p);
      });
      return r;
    }
    bool operator==(const Span&) const = default;
  };
};

template <std::uint32_t Min, std::uint32_t Max, class Set, class Step>
Set gap_repeat(const Set& in, Step&& step) {
  Set cur = in;
  std::uint32_t k = 0;
  for (; k < Min; ++k) {
    Set next = step(cur);
    if (next == cur) return cur;
    cur = std::move(next);
  }
  Set out = cur;
  // Only positions added by a round need to be stepped again.
  for (; !cur.empty() && (Max == 0xffffffffu || k < Max); ++k) {
    cur = step(cur).without(out);
    out.unite(cur);
  }
  return out;
}

// The bytes of the character at `g`: a well-formed scalar whole, else one.
inline TextOffset gap_width(const char* g, TextOffset left) {
  std::size_t len = 0;
  return scalar_at(g, left, len) == kNoScalar ? 1
                                              : static_cast<TextOffset>(len);
}

// The runs of a gap start at its characters, judged on the whole gap.
template <bool Whole, class Match>
bool gap_matches(const char* g, TextOffset n, Match&& match) {
  const auto run = [&](auto in) {
    if constexpr (Whole) {
      in.set(0);
    } else {
      for (TextOffset p = 0; p < n; p += gap_width(g + p, n - p)) in.set(p);
      in.set(n);
    }
    const auto out = match(in);
    return Whole ? out.has(n) : !out.empty();
  };
  if (n < 64) return run(GapPositions::Word{});
  return run(GapPositions::Span{});
}

inline bool gap_end(std::size_t len, const Token& la) {
  return la.sym == kEofSymbol || la.begin >= len;
}

}  // namespace cursiv
