// A `cr<>` spelling read as std::regex (ECMAScript) reads a bracket
// expression, one item at a time: a character, a range, a class escape. A raw
// byte past ASCII is read as UTF-8.
#pragma once

#include <cstddef>
#include <string_view>

#include "cursiv/core/text/scalar_at.hpp"

namespace cursiv {

enum class ClassFault : unsigned char {
  none,
  lone_backslash,
  unknown_escape,
  class_endpoint,
  reversed_range,
  not_scalar,
  malformed,
};

// One character, or a class escape: `klass` is its letter.
struct ClassAtom {
  char32_t value = 0;
  std::size_t width = 1;
  char klass = 0;
  ClassFault fault = ClassFault::none;
};

// The characters `lo`..`hi`, or a class escape.
struct ClassItem {
  char32_t lo = 0;
  char32_t hi = 0;
  char klass = 0;
  bool range = false;
  std::size_t end = 0;
  ClassFault fault = ClassFault::none;
};

// The value of a hexadecimal digit, or -1 for any other character.
constexpr int class_hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

constexpr bool class_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// The character `v`, spelled in `width` bytes, where `v` is a scalar.
constexpr ClassAtom class_scalar(char32_t v, std::size_t width) {
  if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
    return {0, width, 0, ClassFault::not_scalar};
  return {v, width};
}

// The character written as is at `p`, `lead` bytes into its atom.
constexpr ClassAtom class_raw_at(std::string_view s, std::size_t p,
                                 std::size_t lead) {
  if (const auto b = static_cast<unsigned char>(s[p]); b < 0x80)
    return {b, lead + 1};
  std::size_t len = 0;
  const char32_t cp = scalar_at(s.data() + p, s.size() - p, len);
  if (cp == kNoScalar) return {0, lead + 1, 0, ClassFault::malformed};
  return {cp, lead + len};
}

// Plain branches and loops, no closures: a constant evaluation counts every
// step, and a large class reads thousands of atoms.
constexpr ClassAtom class_atom_at(std::string_view s, std::size_t i) {
  if (s[i] != '\\') return class_raw_at(s, i, 0);
  const std::size_t size = s.size();
  if (i + 1 == size) return {0, 1, 0, ClassFault::lone_backslash};
  const char e = s[i + 1];
  const char next = i + 2 < size ? s[i + 2] : '\0';
  switch (e) {
    case 'u':
      if (next == '{') {
        const std::size_t from = i + 3;
        std::size_t k = from;
        char32_t v = 0;
        for (; k < size; ++k) {
          const int d = class_hex_digit(s[k]);
          if (d < 0) break;
          if (v <= 0x10FFFF) v = v * 16 + static_cast<char32_t>(d);
        }
        if (k == from || k == size || s[k] != '}')
          return {0, 2, 0, ClassFault::unknown_escape};
        return class_scalar(v, k + 1 - i);
      }
      [[fallthrough]];
    case 'x': {
      const std::size_t n = e == 'x' ? 2 : 4;
      char32_t v = 0;
      for (std::size_t k = i + 2; k < i + 2 + n; ++k) {
        const int d = k < size ? class_hex_digit(s[k]) : -1;
        if (d < 0) return {0, 2, 0, ClassFault::unknown_escape};
        v = v * 16 + static_cast<char32_t>(d);
      }
      return n == 4 ? class_scalar(v, 6) : ClassAtom{v, 4};
    }
    case 'n':
      return {U'\n', 2};
    case 'r':
      return {U'\r', 2};
    case 't':
      return {U'\t', 2};
    case 'f':
      return {U'\f', 2};
    case 'v':
      return {U'\v', 2};
    case 'b':
      return {U'\b', 2};
    case '0':
      if (next >= '0' && next <= '9')
        return {0, 2, 0, ClassFault::unknown_escape};
      return {0, 2};
    case 'c':
      if (!class_letter(next)) return {0, 2, 0, ClassFault::unknown_escape};
      return {static_cast<char32_t>(next % 32), 3};
    case 'd':
    case 'D':
    case 's':
    case 'S':
    case 'w':
    case 'W':
      return {0, 2, e};
    default:
      break;
  }
  if ((e >= '0' && e <= '9') || class_letter(e))
    return {0, 2, 0, ClassFault::unknown_escape};
  return class_raw_at(s, i + 1, 1);
}

// A `-` is a range operator between two atoms and itself anywhere else.
constexpr ClassItem class_item_at(std::string_view s, std::size_t i) {
  const ClassAtom lo = class_atom_at(s, i);
  const std::size_t mid = i + lo.width;
  if (lo.fault != ClassFault::none) return {.end = mid, .fault = lo.fault};
  if (mid + 1 < s.size() && s[mid] == '-') {
    const ClassAtom hi = class_atom_at(s, mid + 1);
    const std::size_t end = mid + 1 + hi.width;
    if (hi.fault != ClassFault::none) return {.end = end, .fault = hi.fault};
    if (lo.klass != 0 || hi.klass != 0)
      return {.end = end, .fault = ClassFault::class_endpoint};
    if (hi.value < lo.value)
      return {.end = end, .fault = ClassFault::reversed_range};
    return {lo.value, hi.value, 0, true, end};
  }
  return {lo.value, lo.value, lo.klass, false, mid};
}

// Ascending; an upper-case letter names the complement among scalars.
template <class Sink>
constexpr void class_escape_ranges(char klass, Sink sink) {
  struct Span {
    char32_t lo, hi;
  };
  constexpr Span kDigit[] = {{'0', '9'}};
  constexpr Span kWord[] = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
  constexpr Span kSpace[] = {{0x09, 0x0D}, {0x20, 0x20}};
  const auto lower = static_cast<char>(klass | 0x20);
  const Span* r = lower == 'd' ? kDigit : lower == 'w' ? kWord : kSpace;
  const std::size_t n = lower == 'd' ? 1 : lower == 'w' ? 4 : 2;
  if (klass == lower) {
    for (std::size_t k = 0; k < n; ++k) sink(r[k].lo, r[k].hi);
    return;
  }
  char32_t next = 0;
  for (std::size_t k = 0; k < n; ++k) {
    if (r[k].lo > next) sink(next, r[k].lo - 1);
    next = r[k].hi + 1;
  }
  sink(next, char32_t{0x10FFFF});
}

// Every range a spelling names, in spelling order, up to its first fault.
template <class Sink>
constexpr ClassFault for_class_ranges(std::string_view s, Sink sink) {
  for (std::size_t i = 0; i < s.size();) {
    const ClassItem it = class_item_at(s, i);
    if (it.fault != ClassFault::none) return it.fault;
    if (it.klass != 0)
      class_escape_ranges(it.klass, sink);
    else
      sink(it.lo, it.hi);
    i = it.end;
  }
  return ClassFault::none;
}

}  // namespace cursiv
