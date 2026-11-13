#include "cursiv/shared/gen/TextMateKeywords.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/grammar/unicode/UnicodeRanges.hpp"
#include "cursiv/grammar/word_final_in.hpp"
#include "cursiv/services/JsonValue.hpp"
#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/text/scalar_at.hpp"

namespace cursiv::engine {

namespace {

constexpr bool ascii_letter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// What `\w` matches in the editor's regex engine, Oniguruma: a letter, a
// mark, a number or a connector, of any script.
bool word_char(char32_t cp) {
  using namespace unicode;
  return within(kL, std::size(kL), cp) || within(kM, std::size(kM), cp) ||
         within(kN, std::size(kN), cp) || within(kPc, std::size(kPc), cp);
}

constexpr unsigned char ascii_lower(char c) {
  const auto b = static_cast<unsigned char>(c);
  return b >= 'A' && b <= 'Z' ? static_cast<unsigned char>(b - 'A' + 'a') : b;
}

// Dictionary order of the bytes: letter case only decides between words that
// are otherwise equal, the lowercase one first.
bool dictionary_before(std::string_view a, std::string_view b) {
  if (!std::ranges::equal(a, b, {}, ascii_lower, ascii_lower))
    return std::ranges::lexicographical_compare(a, b, {}, ascii_lower,
                                                ascii_lower);
  // A lowercase letter has the higher code.
  return b < a;
}

// `word` as a pattern that matches its text alone.
void append_escaped(std::string& out, std::string_view word) {
  constexpr std::string_view kSpecial = R"(.*+?^${}()|[]\)";
  for (const char c : word) {
    if (kSpecial.find(c) != std::string_view::npos) out += '\\';
    out += c;
  }
}

}  // namespace

std::string textmate_keyword_match(std::vector<TextMateKeyword> keywords) {
  std::erase_if(keywords, [](const TextMateKeyword& k) {
    return std::ranges::none_of(k.text, ascii_letter);
  });
  std::ranges::sort(keywords, [](const TextMateKeyword& a,
                                 const TextMateKeyword& b) {
    return a.text.size() != b.text.size() ? a.text.size() > b.text.size()
                                          : dictionary_before(a.text, b.text);
  });
  // One entry per word: a word some rule reads in any case matches in any
  // case.
  std::vector<TextMateKeyword> words;
  for (const TextMateKeyword& k : keywords)
    if (!words.empty() && words.back().text == k.text)
      words.back().folded = words.back().folded || k.folded;
    else
      words.push_back(k);
  // An empty pattern would stop the colouring of every line.
  if (words.empty()) return "(?!)";
  const bool allFolded = std::ranges::all_of(words, &TextMateKeyword::folded);
  // By the sides that are bounded by `\B`: none, the start, the end, both.
  std::array<std::string, 4> groups;
  for (const TextMateKeyword& w : words) {
    std::size_t len = 0;
    const char32_t first = scalar_at(w.text.data(), w.text.size(), len);
    const char32_t last = cursiv::detail::last_scalar(w.text);
    std::string& group =
        groups[(word_char(first) ? 0 : 1) + (word_char(last) ? 0 : 2)];
    if (!group.empty()) group += '|';
    const bool ownCase = w.folded && !allFolded;
    if (ownCase) group += "(?i:";
    append_escaped(group, w.text);
    if (ownCase) group += ')';
  }
  std::string match;
  for (std::size_t g = 0; g < groups.size(); ++g)
    if (!groups[g].empty())
      match +=
          concat(match.empty() ? "" : "|", (g & 1) != 0 ? R"(\B()" : R"(\b()",
                 groups[g], (g & 2) != 0 ? R"()\B)" : R"()\b)");
  return (allFolded ? "(?i)" : "") + match;
}

std::optional<std::string> with_keyword_match(std::string_view json,
                                              std::string_view match) {
  constexpr std::array<std::string_view, 3> kPath = {"repository", "keywords",
                                                     "match"};
  // Each object or array still open: its closing byte, and whether it lies on
  // `kPath`. A string followed by a colon is the key of the next value.
  std::vector<std::pair<char, bool>> open;
  std::string_view key;
  std::optional<std::pair<std::size_t, std::size_t>> found;
  for (std::size_t at = 0; at < json.size(); ++at) {
    const char c = json[at];
    if (c == '{' || c == '[') {
      open.emplace_back(
          c == '{' ? '}' : ']',
          c == '{' && (open.empty() ||
                       (open.size() < kPath.size() && open.back().second &&
                        key == kPath[open.size() - 1])));
    } else if (c == '}' || c == ']') {
      if (open.empty() || open.back().first != c) return std::nullopt;
      open.pop_back();
    } else if (c == '"') {
      std::size_t end = at + 1;
      while (end < json.size() && json[end] != '"')
        end += json[end] == '\\' ? 2 : 1;
      if (end >= json.size()) return std::nullopt;
      if (const std::size_t next = json.find_first_not_of(" \t\n\r", end + 1);
          next < json.size() && json[next] == ':') {
        key = json.substr(at + 1, end - at - 1);
        // A key that repeats on the path hides what the earlier one held.
        if (!open.empty() && open.size() <= kPath.size() &&
            open.back().second && key == kPath[open.size() - 1])
          found.reset();
      } else if (open.size() == kPath.size() && open.back().second &&
                 key == kPath.back())
        found = {at, end + 1};
      at = end;
    }
  }
  if (!open.empty() || !found) return std::nullopt;
  std::string out(json.substr(0, found->first));
  out += JsonValue(match).toJsonString();
  out += json.substr(found->second);
  return out;
}

}  // namespace cursiv::engine
