#include "cursiv/text/TextBuilder.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <string>

namespace cursiv {

namespace {

// A sign and nineteen digits, or twenty digits.
constexpr std::size_t kLongestNumber = 20;

}  // namespace

std::string TextBuilder::cutFrom(std::size_t at) {
  std::string cut = _text.substr(at);
  _text.resize(at);
  _started = !_text.empty() && _text.back() != '\n';
  return cut;
}

std::string TextBuilder::take() {
  _started = false;
  return std::exchange(_text, {});
}

void TextBuilder::addText(std::string_view text) {
  // A part may view this builder's own text, which the writes below move.
  if (std::less_equal<>()(_text.data(), text.data()) &&
      std::less<>()(text.data(), _text.data() + _text.size()))
    return addText(std::string(text));
  for (auto end = text.find('\n'); end != std::string_view::npos;
       end = text.find('\n')) {
    startLine();
    _text += text.substr(0, end);
    endLine();
    text.remove_prefix(end + 1);
  }
  if (!text.empty()) {
    startLine();
    _text += text;
  }
}

void TextBuilder::addNumber(long long value) {
  std::array<char, kLongestNumber> digits;
  const auto end =
      std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr;
  addText(std::string_view(digits.data(), end));
}

void TextBuilder::addNumber(unsigned long long value) {
  std::array<char, kLongestNumber> digits;
  const auto end =
      std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr;
  addText(std::string_view(digits.data(), end));
}

void TextBuilder::startLine() {
  if (_started) return;
  _started = true;
  if (_indent != 0) _text.append(std::min(_indent, _options.maxIndent), ' ');
}

void TextBuilder::endLine() {
  // No blanks at the end of a line: `npos + 1` is a line of blanks only.
  _text.resize(_text.find_last_not_of(" \t\v\f\r") + 1);
  _text += '\n';
  _started = false;
}

}  // namespace cursiv
