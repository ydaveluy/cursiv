// A C++ string literal for any bytes a grammar may spell: newlines and NULs
// included, since a raw one of those ends the string it is in.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "cursiv/core/text/scalar_at.hpp"

namespace cursiv {

// The bytes spelled as an escape; `??=` is a trigraph to a compiler that still
// reads them.
constexpr std::string_view kPlain = "\\\"?\n\r\t";
constexpr std::string_view kEscaped[] = {R"(\\)", R"(\")", R"(\?)",
                                         R"(\n)", R"(\r)", R"(\t)"};

inline std::string c_literal(std::string_view s) {
  std::string q = "\"";
  for (std::size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (const auto at = kPlain.find(static_cast<char>(c));
        at != std::string_view::npos) {
      q += kEscaped[at];
      ++i;
      continue;
    }
    std::size_t len = 0;
    if (const char32_t scalar =
            c >= 0x80 ? scalar_at(s.data() + i, s.size() - i, len) : kNoScalar;
        scalar != kNoScalar) {
      // A bidirectional control is escaped: a compiler warns of one in a
      // source file, and a reader cannot see what it does to the text around
      // it.
      if (scalar == 0x061C || scalar == 0x200E || scalar == 0x200F ||
          (scalar >= 0x202A && scalar <= 0x202E) ||
          (scalar >= 0x2066 && scalar <= 0x2069))
        q += std::format("\\u{:04x}", static_cast<std::uint32_t>(scalar));
      else
        q.append(s.substr(i, len));
      i += len;
      continue;
    }
    if (c < 0x20 || c >= 0x7f) {
      // Hex runs on: the next byte closes the string and reopens it, so a
      // digit after an escape is a digit and not part of it. A byte that
      // starts no scalar is escaped too: a source file is UTF-8.
      q += std::format(R"(\x{:02x}"")", c);
      ++i;
      continue;
    }
    q += static_cast<char>(c);
    ++i;
  }
  return q + "\"";
}

}  // namespace cursiv
