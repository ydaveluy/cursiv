// The searches a relay runs for its closer of three pieces -- a literal, the
// mark echoed from the source, a literal: a short one by memchr, a long one by
// prefix links, so a repeated partial closer stays linear.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

namespace cursiv {

// The first occurrence of `before + mark + after` in `text` that `take`
// accepts, or null.
const char* lex_find_long(std::string_view text, std::string_view before,
                          std::string_view mark, std::string_view after,
                          function_ref<bool(const char*)> take =
                              [](const char*) { return true; });

// The same, where a closer short enough to copy is found by memchr.
template <class Take>
const char* lex_find_closer(std::string_view text, std::string_view before,
                            std::string_view mark, std::string_view after,
                            Take&& take) {
  const std::size_t m = before.size() + mark.size() + after.size();
  if (m == 0 || m > 128) return lex_find_long(text, before, mark, after, take);
  if (text.size() < m) return nullptr;
  char closer[128];
  std::ranges::copy(
      after,
      std::ranges::copy(mark, std::ranges::copy(before, closer).out).out);
  const char* const last = text.data() + (text.size() - m);
  for (const char* p = text.data(); p <= last; ++p) {
    p = static_cast<const char*>(
        std::memchr(p, closer[0], static_cast<std::size_t>(last - p) + 1u));
    if (p == nullptr) return nullptr;
    if (std::memcmp(p, closer, m) == 0 && take(p)) return p;
  }
  return nullptr;
}

}  // namespace cursiv
