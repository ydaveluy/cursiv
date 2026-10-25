#include "cursiv/lsp/completion/DefaultFuzzyMatcher.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

#include "cursiv/core/text/scalar_at.hpp"

namespace cursiv {

namespace {

// Only ASCII letters fold; all other code units compare unchanged.
constexpr char16_t upper(char16_t c) noexcept {
  return c >= 'a' && c <= 'z' ? static_cast<char16_t>(c - 32) : c;
}

// UTF-16 code units, as a client's string holds them; a byte that starts no
// well-formed sequence reads as U+FFFD.
class Utf16Cursor {
 public:
  explicit Utf16Cursor(std::string_view text) noexcept : _text(text) {}

  [[nodiscard]] bool empty() const noexcept {
    return _text.empty() && _low == 0;
  }

  [[nodiscard]] char16_t next() noexcept {
    if (_low != 0) return std::exchange(_low, char16_t{0});
    std::size_t len = 0;
    char32_t value = scalar_at(_text.data(), _text.size(), len);
    if (len == 0) {
      value = U'\uFFFD';
      len = 1;
    }
    _text.remove_prefix(len);
    if (value <= 0xFFFFu) return static_cast<char16_t>(value);
    value -= 0x10000u;
    _low = static_cast<char16_t>(0xDC00u | (value & 0x3FFu));
    return static_cast<char16_t>(0xD800u | (value >> 10));
  }

 private:
  std::string_view _text;
  char16_t _low = 0;
};

}  // namespace

bool DefaultFuzzyMatcher::isWordTransition(char16_t previous,
                                           char16_t current) const noexcept {
  const bool hump =
      previous >= 'a' && previous <= 'z' && current >= 'A' && current <= 'Z';
  return hump || (previous == '_' && current != '_');
}

bool DefaultFuzzyMatcher::match(std::string_view query,
                                std::string_view text) const {
  if (query.empty()) return true;

  bool anchored = false;
  bool first = true;
  char16_t previous = 0;
  Utf16Cursor queryUnits(query);
  Utf16Cursor textUnits(text);
  char16_t wanted = queryUnits.next();
  while (!textUnits.empty()) {
    const char16_t here = textUnits.next();
    if (upper(here) == upper(wanted)) {
      // The anchor is found once and never lost: after it, every further
      // character of the query may sit anywhere later in the text.
      anchored = anchored || first || isWordTransition(previous, here);
      if (anchored) {
        if (queryUnits.empty()) return true;
        wanted = queryUnits.next();
      }
    }
    previous = here;
    first = false;
  }
  return false;
}

}  // namespace cursiv
