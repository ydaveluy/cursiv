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

constexpr ClassAtom class_atom_at(std::string_view s, std::size_t i) {
  const auto raw = [&](std::size_t p) -> ClassAtom {
    const std::size_t lead = p - i;
    const auto b = static_cast<unsigned char>(s[p]);
    if (b < 0x80) return {b, lead + 1};
    std::size_t len = 0;
    const char32_t cp = scalar_at(s.data() + p, s.size() - p, len);
    if (cp == kNoScalar) return {0, lead + 1, 0, ClassFault::malformed};
    return {cp, lead + len};
  };
  if (s[i] != '\\') return raw(i);
  if (i + 1 == s.size()) return {0, 1, 0, ClassFault::lone_backslash};
  const auto at = [&](std::size_t k) { return k < s.size() ? s[k] : '\0'; };
  const auto digit_of = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
  };
  const auto scalar = [&](char32_t v, std::size_t width) -> ClassAtom {
    if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
      return {0, width, 0, ClassFault::not_scalar};
    return {v, width};
  };
  const auto hex = [&](std::size_t n) -> ClassAtom {
    char32_t v = 0;
    for (std::size_t k = 0; k < n; ++k) {
      const int d = digit_of(at(i + 2 + k));
      if (d < 0) return {0, 2, 0, ClassFault::unknown_escape};
      v = v * 16 + static_cast<char32_t>(d);
    }
    return n == 4 ? scalar(v, 2 + n) : ClassAtom{v, 2 + n};
  };
  const auto braced = [&]() -> ClassAtom {
    char32_t v = 0;
    std::size_t k = 0;
    for (; digit_of(at(i + 3 + k)) >= 0; ++k)
      if (v <= 0x10FFFF)
        v = v * 16 + static_cast<char32_t>(digit_of(at(i + 3 + k)));
    if (k == 0 || at(i + 3 + k) != '}')
      return {0, 2, 0, ClassFault::unknown_escape};
    return scalar(v, 4 + k);
  };
  const char e = s[i + 1];
  const auto digit = [](char c) { return c >= '0' && c <= '9'; };
  const auto letter = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  switch (e) {
    case 'x':
      return hex(2);
    case 'u':
      return at(i + 2) == '{' ? braced() : hex(4);
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
      if (digit(at(i + 2))) return {0, 2, 0, ClassFault::unknown_escape};
      return {0, 2};
    case 'c':
      if (!letter(at(i + 2))) return {0, 2, 0, ClassFault::unknown_escape};
      return {static_cast<char32_t>(at(i + 2) % 32), 3};
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
  if (digit(e) || letter(e)) return {0, 2, 0, ClassFault::unknown_escape};
  return raw(i + 1);
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
