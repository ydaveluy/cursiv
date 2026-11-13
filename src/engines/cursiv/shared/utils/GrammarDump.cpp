#include "cursiv/shared/utils/GrammarDump.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cursiv::engine {
namespace {

// Where a byte of a printed line is: plain, in a doc's prose, or in a quote, a
// class or an escape.
enum class Place : unsigned char { plain, doc, quoted };

// Past the escape at `i`: `\u{...}` reads to its brace, any other is two bytes.
std::size_t escape_end(std::string_view line, std::size_t i) {
  if (!line.substr(i).starts_with("\\u{")) return i + 2;
  const std::size_t close = line.find('}', i + 3);
  return close == std::string_view::npos ? line.size() : close + 1;
}

std::vector<Place> places(std::string_view line) {
  std::vector out(line.size(), Place::plain);
  for (std::size_t i = 0; i < line.size();) {
    const char c = line[i];
    std::size_t end = i + 1;
    Place at = Place::plain;
    if (const bool props = line.substr(i).starts_with("\\p{");
        props || c == '"' || c == '\'' || c == '[') {
      // A quote or a class reads to its close: a class of properties holds
      // bare scalars, quotes and spaces included.
      const char close = props ? '}' : c == '[' ? ']' : c;
      end = props ? i + 3 : i + 1;
      while (end < line.size() && line[end] != close)
        end = line[end] == '\\' ? escape_end(line, end) : end + 1;
      ++end;
      at = Place::quoted;
    } else if (c == '\\') {
      end = escape_end(line, i);
      at = Place::quoted;
    } else if (line.substr(i).starts_with("/**")) {
      // An apostrophe in a doc's prose is no quote.
      const std::size_t close = line.find("*/", i + 3);
      end = close == std::string_view::npos ? line.size() : close + 2;
      at = Place::doc;
    }
    end = std::min(end, line.size());
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(i),
              out.begin() + static_cast<std::ptrdiff_t>(end), at);
    i = end;
  }
  return out;
}

// Each alternative is a contiguous run of `body`: nothing is rewritten, and
// the only byte dropped is the separator itself.
std::vector<std::string_view> top_alternatives(std::string_view body) {
  std::vector<std::string_view> out;
  const std::vector<Place> at = places(body);
  std::size_t begin = 0;
  int depth = 0;
  for (std::size_t i = 0; i < body.size(); ++i) {
    if (at[i] != Place::plain) continue;
    const char c = body[i];
    if (c == '(' || c == '{') {
      ++depth;
    } else if (c == ')' || c == '}') {
      --depth;
      // The separator is exactly ` | `, as `GrammarPrinter` prints a choice.
    } else if (c == '|' && depth == 0 && i > 0 && body[i - 1] == ' ' &&
               i + 1 < body.size() && body[i + 1] == ' ') {
      out.push_back(body.substr(begin, i - begin));
      begin = i + 1;
    }
  }
  out.push_back(body.substr(begin));
  return out;
}

// Breaks on spaces only, never inside a quote or a class. A word longer than
// the width stays whole.
void fill_line(std::string& out, std::string_view text, std::string_view first,
               std::string_view rest, std::size_t width) {
  const std::vector<Place> at = places(text);
  std::string line(first);
  bool empty = true;
  std::size_t begin = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i < text.size() && (text[i] != ' ' || at[i] == Place::quoted)) continue;
    const std::string_view word = text.substr(begin, i - begin);
    if (!empty && line.size() + 1 + word.size() > width) {
      out += line;
      out += '\n';
      line = std::string(rest);
      empty = true;
    }
    if (!empty) line += ' ';
    line += word;
    empty = false;
    begin = i + 1;
  }
  out += line;
  out += '\n';
}

std::string_view trimmed(std::string_view s) {
  const std::size_t b = s.find_first_not_of(' ');
  if (b == std::string_view::npos) return {};
  return s.substr(b, s.find_last_not_of(' ') + 1 - b);
}

}  // namespace

std::string dump_wrapped(std::string_view text, std::size_t width) {
  std::string out;
  std::size_t i = 0;
  while (i <= text.size()) {
    const std::size_t e = text.find('\n', i);
    if (const std::string_view line =
            text.substr(i, e == std::string_view::npos ? e : e - i);
        !line.empty()) {
      if (line.size() <= width) {
        out += line;
        out += '\n';
      } else {
        const std::size_t colon = line.find(": ");
        if (colon == std::string_view::npos) {
          fill_line(out, line, "", "      ", width);
        } else {
          const std::string head(line.substr(0, colon + 2));
          const auto alts = top_alternatives(line.substr(colon + 2));
          for (std::size_t k = 0; k < alts.size(); ++k)
            fill_line(out, trimmed(alts[k]), k == 0 ? head : "    | ", "      ",
                      width);
        }
      }
    }
    if (e == std::string_view::npos) break;
    i = e + 1;
  }
  return out;
}

}  // namespace cursiv::engine
