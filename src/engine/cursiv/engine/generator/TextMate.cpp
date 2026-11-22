#include "cursiv/engine/generator/TextMate.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engine/generator/GrammarPrinter.hpp"
#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/parser/Pack.hpp"
#include "cursiv/grammar/word_final_in.hpp"
#include "cursiv/text/ClassRanges.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/text/unicode/UnicodeRange.hpp"
#include "cursiv/text/unicode/UnicodeRanges.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/JsonValue.hpp"

namespace cursiv::engine::gen {

namespace {

bool has_letter(std::string_view text) {
  while (!text.empty()) {
    std::size_t width = 0;
    const char32_t cp = scalarAt(text.data(), text.size(), width);
    if (unicode::within(unicode::kL, std::size(unicode::kL), cp)) return true;
    text.remove_prefix(width ? width : 1);
  }
  return false;
}

// One Oniguruma character class for the grammar's word continuations.
std::string word_class(const ImageClassDesc& bound) {
  std::string out = bound.complement ? "[^" : "[";
  for (unsigned i = 0; i < std::size(unicode::detail::kNames); ++i)
    if ((bound.props >> i) & 1) {
      out += "\\p{";
      out += unicode::detail::kNames[i];
      out += '}';
    }
  append_class_ranges(out, detail::ClassRanges::of(bound.scalars).r, R"([]\-^&)");
  if (out == "[") return {};
  if (out == "[^") return R"([\s\S])";
  return out + ']';
}

constexpr unsigned char ascii_lower(char c) {
  const auto b = static_cast<unsigned char>(c);
  return b >= 'A' && b <= 'Z' ? static_cast<unsigned char>(b - 'A' + 'a') : b;
}

// Dictionary order of the bytes: letter case only decides between words that
// are otherwise equal, the lowercase one first.
bool dictionary_before(std::string_view a, std::string_view b) {
  const auto order = std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end(),
      [](char x, char y) { return ascii_lower(x) <=> ascii_lower(y); });
  return order == 0 ? b < a : order < 0;
}

// Each folded scalar matches exactly the cases admitted by the lexer.
void append_word(std::string& out, GrammarWord word) {
  constexpr std::string_view kSpecial = R"(.*+?^${}()|[]\)";
  while (!word.text.empty()) {
    std::size_t width;
    const char32_t cp = scalarAt(word.text.data(), word.text.size(), width);
    const auto unit = word.text.substr(0, width ? width : 1);
    if (word.folded && cp != kNoScalar && detail::has_other_case(cp)) {
      out += '[';
      out += unit;
      detail::for_each_other_case(cp, [&](char32_t other) { appendScalar(out, other); });
      out += ']';
    } else {
      if (kSpecial.find(unit.front()) != kSpecial.npos) out += '\\';
      out += unit;
    }
    word.text.remove_prefix(unit.size());
  }
}

// The path keys contain only ASCII letters, written directly or as Unicode escapes.
bool key_names(std::string_view key, std::string_view word) {
  for (const char wanted : word) {
    if (key.starts_with("\\u")) {
      if (key.size() < 6) return false;
      unsigned value;
      const char* end = key.data() + 6;
      const auto [at, ec] = std::from_chars(key.data() + 2, end, value, 16);
      if (ec != std::errc{} || at != end || value != static_cast<unsigned char>(wanted)) return false;
      key.remove_prefix(6);
    } else {
      if (key.empty() || key.front() != wanted) return false;
      key.remove_prefix(1);
    }
  }
  return key.empty();
}

}  // namespace

std::vector<GrammarWord> textmate_keywords(const Cfg& cfg, const Packed& packed) {
  std::vector<bool> shifted(cfg.terminals.size());
  for (const PackedState& state : packed.states)
    for (const PackedAction& action : state.actions)
      if (action.kind == PackedAction::Kind::shift)
        shifted[action.terminal] = true;
  std::vector<GrammarWord> out;
  for (std::size_t t = 0; t < cfg.terminals.size(); ++t) {
    const Terminal& terminal = cfg.terminals[t];
    if (shifted[t] && terminal.trivia == Trivia::none)
      terminal.each_word([&](const GrammarWord& word) { out.push_back(word); });
  }
  return out;
}

std::string textmate_keyword_match(std::vector<GrammarWord> words, const ImageClassDesc& bound) {
  std::erase_if(words, [](const GrammarWord& k) { return !has_letter(k.text); });
  std::ranges::sort(words, [](const GrammarWord& a, const GrammarWord& b) {
    return a.text.size() != b.text.size() ? a.text.size() > b.text.size()
                                          : dictionary_before(a.text, b.text);
  });
  // Equal spellings share the broadest case setting.
  auto end = words.begin();
  for (const GrammarWord& word : words) {
    if (end != words.begin() && end[-1].text == word.text) end[-1].folded |= word.folded;
    else *end++ = word;
  }
  words.erase(end, words.end());
  // An empty pattern would stop the colouring of every line.
  if (words.empty()) return "(?!)";
  const std::string chars = word_class(bound);
  std::string match = chars.empty() ? "" : "(?<!" + chars + ")";
  match += '(';
  const auto guarded = [&](const GrammarWord& w) {
    return detail::word_final_in(w.text, bound.props, bound.complement, bound.scalars, w.folded);
  };
  const std::string right = "(?!" + chars + ")";
  for (auto at = words.begin(); at != words.end();) {
    if (at != words.begin()) match += '|';
    const bool guard = guarded(*at);
    const auto next = std::find_if(at + 1, words.end(), [&](const auto& w) { return guarded(w) != guard; });
    const bool whole = at == words.begin() && next == words.end();
    const bool group = guard && !whole && next - at > 1;
    if (group) match += "(?:";
    for (;;) {
      append_word(match, *at++);
      if (at == next) break;
      match += '|';
    }
    if (group) match += ')';
    if (whole) return guard ? match + ')' + right : match + ')';
    if (guard) match += right;
  }
  return match + ')';
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
                        key_names(key, kPath[open.size() - 1]))));
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
            open.back().second && key_names(key, kPath[open.size() - 1]))
          found.reset();
      } else if (open.size() == kPath.size() && open.back().second &&
                 key_names(key, kPath.back())) {
        found = {at, end + 1};
      }
      at = end;
    }
  }
  if (!open.empty() || !found) return std::nullopt;
  std::string out(json.substr(0, found->first));
  out += JsonValue(match).toJsonString();
  out += json.substr(found->second);
  return out;
}

}  // namespace cursiv::engine::gen
