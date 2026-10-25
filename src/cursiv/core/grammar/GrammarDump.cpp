#include "cursiv/core/grammar/GrammarDump.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cursiv {
namespace {

// Each alternative is a contiguous run of `body`: nothing is rewritten, and
// the only byte dropped is the separator itself.
std::vector<std::string_view> top_alternatives(std::string_view body) {
  std::vector<std::string_view> out;
  std::size_t begin = 0;
  int depth = 0;
  char quote = 0;
  bool esc = false;
  bool in_class = false;
  for (std::size_t i = 0; i < body.size(); ++i) {
    const char c = body[i];
    if (esc) {
      esc = false;
    } else if (c == '\\') {
      esc = true;
    } else if (quote != 0) {
      if (c == quote) quote = 0;
    } else if (in_class) {
      if (c == ']') in_class = false;
    } else if (body.substr(i).starts_with("/**")) {
      // A doc's prose is printed as written: an apostrophe in it is no quote.
      const std::size_t close = body.find("*/", i + 3);
      if (close == std::string_view::npos) break;
      i = close + 1;
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '[') {
      in_class = true;
    } else if (c == '(' || c == '{') {
      ++depth;
    } else if (c == ')' || c == '}') {
      --depth;
      // The separator is exactly ` | `, which is what `Printer<Choice>` emits;
      // a doc's own pipes never wear both spaces, so prose passes through whole.
    } else if (c == '|' && depth == 0 && i > 0 && body[i - 1] == ' ' &&
               i + 1 < body.size() && body[i + 1] == ' ') {
      out.push_back(body.substr(begin, i - begin));
      begin = i + 1;
    }
  }
  out.push_back(body.substr(begin));
  return out;
}

// Broken on spaces only, so nothing a reader needs to see is ever split.
void fill_line(std::string& out, std::string_view text, std::string_view first,
               std::string_view rest, std::size_t width) {
  std::string line(first);
  bool empty = true;
  std::size_t i = 0;
  while (i < text.size()) {
    const std::size_t e = text.find(' ', i);
    const std::string_view word =
        text.substr(i, e == std::string_view::npos ? e : e - i);
    if (!empty && line.size() + 1 + word.size() > width) {
      out += line;
      out += '\n';
      line = std::string(rest);
      empty = true;
    }
    if (!empty) line += ' ';
    line += word;
    empty = false;
    if (e == std::string_view::npos) break;
    i = e + 1;
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
    const std::string_view line =
        text.substr(i, e == std::string_view::npos ? e : e - i);
    if (!line.empty()) {
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

}  // namespace cursiv
