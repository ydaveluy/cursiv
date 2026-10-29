// What the LR lanes ask of a parse: whether it read as expected, clean or
// repaired, the leaves it laid, and what a caret is offered.
#pragma once

#include <cstdio>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"

namespace checks {

// Whether `got`, read off `text`, is `want`; a mismatch is printed.
inline bool matches(std::string_view text, const std::string& got,
                    std::string_view want) {
  if (got == want) return true;
  std::printf("  %s -> %s\n", std::string(text).c_str(), got.c_str());
  return false;
}

// Accepted with no diagnostic, and a tree.
inline bool clean(const cursiv::ParseResult& r) {
  return r.accepted && r.diagnostics.empty() && r.value != nullptr;
}
inline bool clean(const cursiv::Parser& p, std::string_view text) {
  return clean(p.parse(text));
}
// Accepted, with some repair.
inline bool repaired(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse(text);
  return r.accepted && !r.diagnostics.empty();
}

// The leaves in document order, a kept trivia in brackets.
inline std::string leaves(const cursiv::Parser& p, std::string_view text) {
  const cursiv::ParseResult r = p.parse(text);
  std::string out;
  r.cst().visit_leaves([&](const cursiv::CstLeaf& l) {
    if (l.begin == l.end) return;
    if (!out.empty()) out += ' ';
    const std::string s(text.substr(l.begin, l.end - l.begin));
    out += l.hidden ? "[" + s + "]" : s;
  });
  return out;
}

// What the caret over [from, to] of `r` is offered.
inline std::vector<cursiv::NextFeature> features(const cursiv::Parser& p,
                                                 const cursiv::ParseResult& r,
                                                 cursiv::TextOffset from,
                                                 cursiv::TextOffset to) {
  std::vector<cursiv::NextFeature> out;
  p.nextFeaturesAt(r.cst(), from, to, out);
  return out;
}
// Whether one of them is spelled `literal`.
inline bool spelled(const cursiv::Parser& p,
                    std::span<const cursiv::NextFeature> out,
                    std::string_view literal) {
  for (const cursiv::NextFeature& f : out)
    for (const std::string_view s : p.getGrammar().terminalSpellings(f.feature))
      if (s == literal) return true;
  return false;
}
// Whether one of them is the terminal named `name`.
inline bool named(const cursiv::Parser& p,
                  std::span<const cursiv::NextFeature> out,
                  std::string_view name) {
  for (const cursiv::NextFeature& f : out)
    if (p.getGrammar().terminalName(f.feature) == name) return true;
  return false;
}
// Whether the caret over [from, to] of `text` is offered `literal`, or the
// terminal named `name`.
inline bool offers(const cursiv::Parser& p, std::string_view text,
                   cursiv::TextOffset from, cursiv::TextOffset to,
                   std::string_view literal) {
  return spelled(p, features(p, p.parse(text), from, to), literal);
}
inline bool offers_named(const cursiv::Parser& p, std::string_view text,
                         cursiv::TextOffset from, cursiv::TextOffset to,
                         std::string_view name) {
  return named(p, features(p, p.parse(text), from, to), name);
}

// The literals a caret at the end of `text` is offered, each once.
inline std::string offered(const cursiv::Parser& p, std::string_view text) {
  const auto at = static_cast<cursiv::TextOffset>(text.size());
  std::string seen;
  for (const cursiv::NextFeature& f : features(p, p.parse(text), at, at))
    for (const std::string_view t : p.getGrammar().terminalSpellings(f.feature))
      if (seen.find(t) == std::string::npos) seen += t;
  return seen;
}

// Every spelling the caret at `at` of `text` is offered.
inline std::set<std::string> spellings(const cursiv::Parser& p,
                                       std::string_view text,
                                       cursiv::TextOffset at) {
  std::set<std::string> words;
  for (const cursiv::NextFeature& f : features(p, p.parse(text), at, at))
    for (const std::string_view s : p.getGrammar().terminalSpellings(f.feature))
      words.emplace(s);
  return words;
}

// A text whose caret a `|` marks, the mark cut out.
struct Marked {
  std::string text;
  cursiv::TextOffset at;
  explicit Marked(std::string marked)
      : text(std::move(marked)),
        at(static_cast<cursiv::TextOffset>(text.find('|'))) {
    text.erase(at, 1);
  }
};

}  // namespace checks
