// A text lowered as a client's `toLowerCase` lowers it: each scalar by its
// full mapping, a capital sigma by whether it closes a word.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

#include "cursiv/core/grammar/unicode/UnicodeLower.hpp"
#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/core/text/scalar_at.hpp"

namespace cursiv {

namespace detail {

inline char32_t lower_scalar(char32_t cp) noexcept {
  const auto* run = std::upper_bound(
      std::begin(unicode::kLower), std::end(unicode::kLower), cp,
      [](char32_t c, const unicode::LowerRun& r) { return c < r.first; });
  if (run == std::begin(unicode::kLower)) return cp;
  --run;
  if (cp > run->last || (cp - run->first) % run->stride != 0) return cp;
  return static_cast<char32_t>(static_cast<std::int32_t>(cp) + run->delta);
}

inline bool cased(char32_t cp) noexcept {
  return unicode::within(unicode::kCased, std::size(unicode::kCased), cp);
}

inline bool case_ignorable(char32_t cp) noexcept {
  return unicode::within(unicode::kCaseIgnorable,
                         std::size(unicode::kCaseIgnorable), cp);
}

// Whether the sigma at `[begin, end)` follows a cased letter and precedes
// none, case-ignorable scalars aside.
inline bool final_sigma(std::string_view s, std::size_t begin,
                        std::size_t end) noexcept {
  bool preceded = false;
  for (std::size_t i = begin; i > 0;) {
    std::size_t b = i - 1;
    while (b > 0 && (static_cast<unsigned char>(s[b]) & 0xC0u) == 0x80u) --b;
    std::size_t len = 0;
    const char32_t cp = scalar_at(s.data() + b, s.size() - b, len);
    if (len == 0 || b + len != i) break;
    if (!case_ignorable(cp)) {
      preceded = cased(cp);
      break;
    }
    i = b;
  }
  if (!preceded) return false;
  for (std::size_t i = end; i < s.size();) {
    std::size_t len = 0;
    const char32_t cp = scalar_at(s.data() + i, s.size() - i, len);
    if (len == 0) return true;
    if (!case_ignorable(cp)) return !cased(cp);
    i += len;
  }
  return true;
}

}  // namespace detail

// A byte that starts no well-formed sequence is kept as it is.
inline std::string lower_case(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    std::size_t len = 0;
    const char32_t cp = scalar_at(text.data() + i, text.size() - i, len);
    if (len == 0) {
      out += text[i++];
      continue;
    }
    if (cp == 0x130)
      out += "i\xCC\x87";
    else if (cp == 0x3A3)
      append_scalar(out, detail::final_sigma(text, i, i + len) ? 0x3C2 : 0x3C3);
    else
      append_scalar(out, detail::lower_scalar(cp));
    i += len;
  }
  return out;
}

}  // namespace cursiv
