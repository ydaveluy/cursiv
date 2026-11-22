// A table of constants, as the generated code declares one.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace cursiv::engine::gen {

// `text` as a C++ string literal.
inline std::string cpp_literal(std::string_view text) {
  static constexpr char kDigits[] = "01234567";
  std::string out = "\"";
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += ch;
    } else if (c >= 0x20 && c < 0x7F && c != '?') {
      out += ch;
    } else {
      // Three octal digits: nothing after them reads as one more.
      out += '\\';
      out += kDigits[c >> 6];
      out += kDigits[(c >> 3) & 7];
      out += kDigits[c & 7];
    }
  }
  return out + "\"";
}

// Appends `constexpr type name[] = {...};` to `out`, `perLine` values a
// line: numbers, or the text of expressions. A table of nothing holds
// `filler`: no array holds nothing.
template <class Values>
void emit_array(std::string& out, std::string_view type, std::string_view name,
                const Values& values, std::size_t perLine, std::string_view filler = "0") {
  out += "constexpr ";
  out += type;
  out += " ";
  out += name;
  out += "[] = {";
  std::size_t count = 0;
  for (const auto& value : values) {
    out += count++ % perLine == 0 ? "\n    " : " ";
    if constexpr (std::is_arithmetic_v<std::remove_cvref_t<decltype(value)>>) {
      out += std::to_string(value);
      // A word past the signed range is no decimal constant without its
      // suffix.
      if (sizeof(value) == 8) out += "u";
    } else {
      out += value;
    }
    out += ",";
  }
  if (count == 0) {
    out += "\n    ";
    out += filler;
    out += ",";
  }
  out += "\n};\n";
}

}  // namespace cursiv::engine::gen
