#include "cursiv/text/TextBuilder.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace cursiv {

namespace {

// A sign and nineteen digits, or twenty digits.
constexpr std::size_t kLongestNumber = 20;

}  // namespace

std::string TextBuilder::cut_from(std::size_t at) {
  std::string cut = _text.substr(at);
  _text.resize(at);
  _started = !_text.empty() && _text.back() != '\n';
  return cut;
}

std::string TextBuilder::take() {
  _started = false;
  return std::exchange(_text, {});
}

void TextBuilder::add_text(std::string_view text) {
  for (auto end = text.find('\n'); end != std::string_view::npos;
       end = text.find('\n')) {
    start_line();
    _text += text.substr(0, end);
    end_line();
    text.remove_prefix(end + 1);
  }
  if (!text.empty()) {
    start_line();
    _text += text;
  }
}

void TextBuilder::add_number(long long value) {
  std::array<char, kLongestNumber> digits;
  const auto end =
      std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr;
  add_text(std::string_view(digits.data(), end));
}

void TextBuilder::add_number(unsigned long long value) {
  std::array<char, kLongestNumber> digits;
  const auto end =
      std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr;
  add_text(std::string_view(digits.data(), end));
}

void TextBuilder::start_line() {
  if (_started) return;
  _started = true;
  if (_indent != 0) _text.append(std::min(_indent, _options.maxIndent), ' ');
}

void TextBuilder::end_line() {
  // No blanks at the end of a line: `npos + 1` is a line of blanks only.
  _text.resize(_text.find_last_not_of(" \t\v\f\r") + 1);
  _text += '\n';
  _started = false;
}

}  // namespace cursiv
