// The searches a relay runs for its closer: one literal by memchr, or a
// closer of three pieces -- a literal, the mark echoed from the source, a
// literal -- by prefix links, so a repeated partial closer stays linear.
#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

// The first occurrence of `needle` in `hay`, or null.
inline const char* lex_find(const char* hay, TextOffset n, const char* needle,
                            TextOffset m) {
  if (m == 0) return hay;
  if (n < m) return nullptr;
  const auto* const last = hay + (n - m);
  const char* p = hay;
  while (p <= last) {
    p = static_cast<const char*>(
        std::memchr(p, needle[0], static_cast<std::size_t>(last - p) + 1u));
    if (p == nullptr) return nullptr;
    if (std::memcmp(p, needle, m) == 0) return p;
    ++p;
  }
  return nullptr;
}

const char* lex_find_long(std::string_view text, std::string_view before,
                          std::string_view mark, std::string_view after);

}  // namespace cursiv
