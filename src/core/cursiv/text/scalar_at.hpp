// The scalar a UTF-8 sequence encodes, read once for every reader of a text:
// well-formed sequences only, so the lexer, a guard and a spelling agree.
#pragma once

#include <cstddef>
#include <string>

namespace cursiv {

inline constexpr char32_t kNoScalar = 0xFFFFFFFFu;
// A class's list of scalars names, past them, `kBackScalar + k` for the scalar
// that starts `k` bytes before the byte a bound reads.
inline constexpr char32_t kBackScalar = 0xFFFFFFF0u;
// The bytes the longest well-formed sequence takes.
inline constexpr std::size_t kScalarBytes = 4;

// The scalar at `p` and, in `len`, the bytes it takes; `kNoScalar` and no
// bytes where they are not one -- a stray continuation, a truncated tail, an
// overlong form, a surrogate.
constexpr char32_t scalar_at(const char* p, std::size_t avail,
                             std::size_t& len) {
  len = 0;
  if (avail == 0) return kNoScalar;
  const auto b0 = static_cast<unsigned char>(p[0]);
  const std::size_t n = b0 < 0x80 ? 1 : b0 < 0xC2 ? 0 : b0 < 0xE0 ? 2
                                     : b0 < 0xF0 ? 3 : b0 < 0xF5 ? 4 : 0;
  if (n == 0 || avail < n) return kNoScalar;
  char32_t cp = n == 1 ? b0 : b0 & (0x7Fu >> n);
  for (std::size_t i = 1; i < n; ++i) {
    const auto b = static_cast<unsigned char>(p[i]);
    if ((b & 0xC0u) != 0x80u) return kNoScalar;
    cp = (cp << 6) | (b & 0x3Fu);
  }
  if ((n == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
      (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)))
    return kNoScalar;
  len = n;
  return cp;
}

// Whether a well-formed scalar begun before `at` runs past it.
constexpr bool inside_scalar(const char* src, std::size_t at,
                             std::size_t avail) {
  for (std::size_t k = 1; k <= 3 && k <= at; ++k) {
    std::size_t width = 0;
    if (scalar_at(src + at - k, avail - (at - k), width) != kNoScalar &&
        width > k)
      return true;
  }
  return false;
}

// The UTF-8 form of a scalar, appended to `out`.
inline void append_scalar(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
    return;
  }
  if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  }
  out += static_cast<char>(0x80 | (cp & 0x3F));
}

}  // namespace cursiv
