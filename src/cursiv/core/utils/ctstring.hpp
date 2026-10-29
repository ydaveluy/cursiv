#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "cursiv/core/text/ClassItem.hpp"

namespace cursiv {

// A string literal usable as a template argument, so `kw<"class">` names a type
// and two spellings of one keyword are the same type.
template <std::size_t N>
struct str {
  char data[N]{};

  constexpr str(const char (&s)[N]) {
    for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
  }
  static constexpr std::size_t size() { return N - 1; }
  constexpr char operator[](std::size_t i) const { return data[i]; }
  constexpr std::string_view view() const { return {data, N - 1}; }
};

inline constexpr auto kLowerTable = [] {
  std::array<char, 256> t{};
  for (int i = 0; i < 256; ++i)
    t[static_cast<std::size_t>(i)] = (i >= 'A' && i <= 'Z')
                                         ? static_cast<char>(i - 'A' + 'a')
                                         : static_cast<char>(i);
  return t;
}();

constexpr char to_lower(char c) {
  return kLowerTable[static_cast<unsigned char>(c)];
}

// A flat 256-entry table, so `test` is one indexed byte load.
struct CharSet {
  bool table[256]{};

  constexpr void set(unsigned char c) { table[c] = true; }
  constexpr bool test(unsigned char c) const { return table[c]; }

  constexpr CharSet& merge(const CharSet& other) {
    for (int i = 0; i < 256; ++i) table[i] = table[i] || other.table[i];
    return *this;
  }

  friend constexpr CharSet operator|(CharSet a, const CharSet& b) {
    a.merge(b);
    return a;
  }
  friend constexpr CharSet operator~(CharSet a) {
    for (bool& v : a.table) v = !v;
    return a;
  }
  constexpr std::size_t count() const {
    std::size_t n = 0;
    for (bool i : table) n += i ? 1 : 0;
    return n;
  }
};

namespace detail {
// Not constexpr on purpose: reached from a constant evaluation, its name is the
// diagnostic.
inline void malformed_cr_spec() { /* the call is the diagnostic */ }

// The bytes of a spelling of ASCII characters, on its text rather than on its
// `str` type: one body for every spelling instead of one instantiation each.
constexpr CharSet parse_charset(std::string_view Spec) {
  CharSet cs{};
  const ClassFault fault =
      for_class_ranges(Spec, [&cs](char32_t lo, char32_t hi) {
        if (hi > 0x7F) malformed_cr_spec();
        for (char32_t v = lo; v <= hi; ++v)
          cs.set(static_cast<unsigned char>(v));
      });
  if (fault != ClassFault::none) malformed_cr_spec();
  return cs;
}
}  // namespace detail

template <str Spec>
constexpr CharSet make_charset() {
  return detail::parse_charset(Spec.view());
}

}  // namespace cursiv
