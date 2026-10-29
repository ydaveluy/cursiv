// A type as the emitted unit must read it back. The unit is written at global
// scope, so a name is qualified from there -- its own namespace would shadow
// the author's (`json::generated::JSON::json`).
#pragma once

#include <cstddef>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/TypeName.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"

namespace cursiv {

// A value of class type prints as `T(...)`, which a template argument list
// reads back as a type: the parentheses make it the expression it is.
inline std::string gen_enclose(std::string_view s) {
  std::string out;
  std::size_t i = 0;
  while (i < s.size()) {
    if (const auto quoted = detail::cpp_literal_end(s, i); quoted != i) {
      out += s.substr(i, quoted - i);
      i = quoted;
      continue;
    }
    out += s[i++];
    if (s[i - 1] != '<') continue;
    std::size_t start = i - 1;
    while (start != 0) {
      if (const char c = s[start - 1];
          !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == ':'))
        break;
      --start;
    }
    std::string_view name = s.substr(start, i - 1 - start);
    if (name.starts_with("::")) name.remove_prefix(2);
    const bool repeat = name == "cursiv::Repeat";
    std::size_t argument = 0;
    for (;;) {
      std::size_t j = i, depth = 0;
      while (j < s.size()) {
        if (const auto quoted = detail::cpp_literal_end(s, j); quoted != j) {
          j = quoted;
          continue;
        }
        if (const char c = s[j]; c == '<' || c == '(' || c == '{' || c == '[') {
          ++depth;
        } else if (c == '>' || c == ')' || c == '}' || c == ']') {
          if (depth == 0) break;
          --depth;
        } else if (c == ',' && depth == 0) {
          break;
        }
        ++j;
      }
      std::string arg = gen_enclose(s.substr(i, j - i));
      if (repeat && argument == 1) {
        // The unbounded limit belongs to the target's size_t, not the host's.
        const auto first = arg.find_first_not_of(" \t");
        const auto last = arg.find_last_not_of(" \t");
        if (first != std::string::npos) {
          const std::string_view bound =
              std::string_view(arg).substr(first, last - first + 1);
          const auto digits = bound.find_first_not_of("0123456789");
          if (bound.substr(0, digits) ==
                  std::to_string(std::numeric_limits<std::size_t>::max()) &&
              (digits == std::string_view::npos ||
               bound.substr(digits).find_first_not_of("uUlL") ==
                   std::string_view::npos))
            arg.replace(first, bound.size(), "::cursiv::kUnbounded");
        }
      }
      ++argument;
      if (const std::size_t e = arg.find_last_not_of(' ');
          e != std::string::npos && e >= 1 && arg[e] == ')' &&
          arg[e - 1] == '(')
        arg = "(" + arg + ")";
      out += arg;
      i = j;
      if (i == s.size() || s[i] != ',') break;
      out += s[i++];
    }
  }
  return out;
}

// Wide integer template arguments need literals both compilers can read.
inline std::string gen_widen(std::string text) {
  for (std::size_t i = 0; i < text.size();) {
    if (const auto quoted = detail::cpp_literal_end(text, i); quoted != i) {
      i = quoted;
      continue;
    }
    const auto word = [](char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
             (c >= 'A' && c <= 'Z') || c == '_';
    };
    if (text[i] < '0' || text[i] > '9' || (i != 0 && word(text[i - 1]))) {
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < text.size() && text[j] >= '0' && text[j] <= '9') ++j;
    const bool wide =
        j - i > 19 ||
        (j - i == 19 && text.compare(i, 19, "9223372036854775807") > 0);
    const bool negativeMinimum =
        i != 0 && text[i - 1] == '-' &&
        text.compare(i, j - i, "9223372036854775808") == 0;
    const std::size_t digitEnd = j;
    if (negativeMinimum)
      while (j < text.size() && (text[j] == 'l' || text[j] == 'L')) ++j;
    if (wide && (j == text.size() || !word(text[j]))) {
      if (negativeMinimum) {
        const std::string minimum = std::format(
            "(-9223372036854775807{} - 1)", j - digitEnd == 1 ? "l" : "ll");
        text.replace(i - 1, j - i + 1, minimum);
        j = i - 1 + minimum.size();
      } else {
        text.insert(j, "ull");
        j += 3;
      }
    }
    i = j;
  }
  return text;
}

// Template arguments ride along as written: the unit is emitted at global
// scope, so they resolve there.
inline std::string gen_qualified(std::string_view name) {
  return gen_widen(gen_enclose(name == "void" || name.starts_with("::") ? std::string(name)
                                              : "::" + std::string(name)));
}

// The narrowings are spelled qualified already.
inline std::string narrow_list(const auto& narrows) {
  return "::cursiv::TypeList<" + join(narrows, ", ") + ">";
}

}  // namespace cursiv
