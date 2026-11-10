// The scalar a UTF-8 sequence encodes, read once for every reader of a text:
// well-formed sequences only, so the lexer, a guard and a spelling agree; and
// `quote`, a text spelled for people to read.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace cursiv {

inline constexpr char32_t kNoScalar = 0xFFFFFFFFu;

// The scalar at `p` and, in `len`, the bytes it takes; `kNoScalar` and no
// bytes where they are not one -- a lone continuation, a truncated tail, an
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

// `text` between two `q`s, as every message shows a text: C++ escapes, and
// any other control byte in octal, which no digit after it extends.
inline std::string quote(std::string_view text, char q = '\'') {
  std::string out(1, q);
  for (const char ch : text) {
    if (ch == q) {
      out += '\\';
      out += ch;
      continue;
    }
    switch (ch) {
      case '\\':
        out += R"(\\)";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (const auto byte = static_cast<unsigned char>(ch);
            byte < 32 || byte == 127)
          out += {'\\', static_cast<char>('0' + (byte >> 6)),
                  static_cast<char>('0' + ((byte >> 3) & 7)),
                  static_cast<char>('0' + (byte & 7))};
        else
          out += ch;
    }
  }
  out += q;
  return out;
}

}  // namespace cursiv
