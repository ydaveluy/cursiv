#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

// The line terminators as the document's line index counts them: "\n", a
// lone "\r", and "\r\n" once.

namespace cursiv {

struct LineTerminator {
  std::size_t position;
  std::size_t length;
};

[[nodiscard]] constexpr std::optional<LineTerminator> find_next_line_terminator(
    std::string_view text, std::size_t from) noexcept {
  for (std::size_t index = from; index < text.size(); ++index) {
    if (text[index] == '\n')
      return LineTerminator{.position = index, .length = 1};
    if (text[index] == '\r') {
      const bool crlf = index + 1 < text.size() && text[index + 1] == '\n';
      return LineTerminator{.position = index, .length = crlf ? 2u : 1u};
    }
  }
  return std::nullopt;
}

// The first terminator's own bytes; "\n" for a text holding none.
[[nodiscard]] constexpr std::string_view detect_newline_style(
    std::string_view text) noexcept {
  if (const auto terminator = find_next_line_terminator(text, 0))
    return text.substr(terminator->position, terminator->length);
  return "\n";
}

[[nodiscard]] constexpr bool is_horizontal_whitespace(char ch) noexcept {
  return ch == ' ' || ch == '\t';
}

}  // namespace cursiv
