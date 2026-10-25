// The three-piece search, its checks kept out of the loop they guard.
#include "cursiv/engines/shared/runtime/lex_find.hpp"

#include <cstddef>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/macros.hpp"

namespace cursiv {

// Isolate validation so its range checks do not reshape the search loops.
CURSIV_NOINLINE static const char* lex_find_kmp(
    std::string_view text, std::string_view before, std::string_view mark,
    std::string_view after) {
  const auto size = before.size() + mark.size() + after.size();
  if (size == 0) return text.data();
  const auto byte = [&](std::size_t i) {
    if (i < before.size()) return before[i];
    i -= before.size();
    return i < mark.size() ? mark[i] : after[i - mark.size()];
  };
  // Prefix links keep repeated partial closers linear without copying the mark.
  std::vector<TextOffset> prefix(size);
  for (std::size_t i = 1, j = 0; i < size; ++i) {
    while (j != 0 && byte(i) != byte(j)) j = prefix[j - 1];
    if (byte(i) == byte(j)) ++j;
    prefix[i] = static_cast<TextOffset>(j);
  }
  for (std::size_t i = 0, j = 0; i < text.size(); ++i) {
    while (j != 0 && text[i] != byte(j)) j = prefix[j - 1];
    if (text[i] == byte(j)) ++j;
    if (j == size) return text.data() + i + 1 - size;
  }
  return nullptr;
}

const char* lex_find_long(std::string_view text, std::string_view before,
                          std::string_view mark, std::string_view after) {
  if (before.size() > text.size() || mark.size() > text.size() - before.size() ||
      after.size() > text.size() - before.size() - mark.size())
    return nullptr;
  return lex_find_kmp(text, before, mark, after);
}

}  // namespace cursiv
