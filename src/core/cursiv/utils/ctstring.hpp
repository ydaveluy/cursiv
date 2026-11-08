#pragma once

#include <cstddef>
#include <string_view>

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

}  // namespace cursiv
