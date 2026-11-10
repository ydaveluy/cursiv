#include "cursiv/parser/DefaultParserErrorMessageProvider.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"

namespace cursiv {

namespace {

// What to call the element where its text cannot be guessed.
std::string_view named_below(const Grammar& grammar, ElementId element) {
  if (element == kNoElement) return {};
  return grammar.terminalName(element);
}

// The bytes a line's indentation is made of, and those a quote trims.
constexpr std::string_view kIndentBlanks = " \t\f\v";
constexpr std::string_view kBlanks = " \t\f\v\n\r";

// One line of source, whatever the author wrote: a repair over a run of
// newlines would otherwise print a paragraph into a hover.
std::string quote_found(std::string_view text) {
  constexpr std::size_t kMax = 24;
  const std::size_t begin = text.find_first_not_of(kBlanks);
  if (begin == std::string::npos) return {};
  text = text.substr(begin, text.find_last_not_of(kBlanks) - begin + 1);
  const bool shortened = text.size() > kMax;
  std::size_t cut = std::min(text.size(), kMax);
  // Keep the truncation on a UTF-8 lead byte.
  if (shortened)
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U)
      --cut;
  std::string out(text.substr(0, cut));
  for (char& c : out)
    if (kBlanks.find(c) != std::string_view::npos) c = ' ';
  if (shortened) out += "...";
  return out;
}

// The start of the line that holds `at`.
std::size_t line_start(std::string_view text, std::size_t at) {
  while (at > 0 && text[at - 1] != '\n' && text[at - 1] != '\r') --at;
  return at;
}

// The end of the leading blanks from `at`.
std::size_t blanks_end(std::string_view text, std::size_t at) {
  return std::min(text.find_first_not_of(kIndentBlanks, at), text.size());
}

// Whether the diagnostic's span lies in `text`.
bool in_text(std::string_view text, const ParseDiagnostic& d) {
  return d.begin <= d.end && d.end <= text.size();
}

// A deletion that starts in a line's leading blanks and takes some of them,
// or one just before them where a dropped indent took no byte: it rejects the
// line's indentation, and any text it goes on over.
bool starts_indentation(std::string_view text, const ParseDiagnostic& d) {
  const std::size_t head = line_start(text, d.begin);
  return blanks_end(text, head) >= d.begin &&
         blanks_end(text, d.begin) > d.begin;
}

// `'a'`, `'a' or 'b'`, `one of 'a', 'b', 'c'`.
std::string spell_expected(std::span<const std::string_view> spellings) {
  if (spellings.empty()) return {};
  std::string out = spellings.size() > 2 ? "one of " : "";
  for (std::size_t at = 0; at < spellings.size(); ++at) {
    if (at != 0) out += spellings.size() == 2 ? " or " : ", ";
    out += std::format("'{}'", spellings[at]);
  }
  return out;
}

// What a repair's sentence is made of: the text it went over, quoted, and
// what was expected there, the expectation before the finding.
struct Wording {
  std::string found;
  std::string expected;

  [[nodiscard]] std::string expecting() const {
    return expected.empty() ? std::string("Syntax error")
                            : "Expecting " + expected + ".";
  }
  // The kinds with found text share one sentence; only the order of its two
  // halves differs.
  [[nodiscard]] std::string opposed(bool foundFirst) const {
    if (found.empty()) return expecting();
    if (expected.empty()) return "Unexpected '" + found + "'.";
    return foundFirst
               ? "Unexpected '" + found + "', expecting " + expected + "."
               : "Expecting " + expected + " but found '" + found + "'.";
  }
};

// A position whose spellings the grammar cannot enumerate -- an identifier, a
// number -- falls back to the element's own name, and a position that
// answers neither keeps the bare sentence.
Wording wording(const Grammar& grammar, const ParseDiagnostic& d,
                std::string_view text) {
  constexpr std::size_t kMaxNamed = 4;
  const std::string_view over = in_text(text, d)
                                    ? text.substr(d.begin, d.end - d.begin)
                                    : std::string_view{};
  Wording w;
  // A skip over whitespace only, the one thing a tight region refuses, quotes
  // to nothing: `found` is empty and only the expectation is said.
  w.found = quote_found(over);
  // A found token that is one of the expected ones is not named twice:
  // "Unexpected '+'." says it all.
  const std::vector<std::string_view> named =
      expected_spellings(grammar, d, kMaxNamed);
  const bool echoes =
      !w.found.empty() &&
      std::ranges::find(named, std::string_view(w.found)) != named.end();
  if (!echoes) w.expected = spell_expected(named);
  // A position with no spelling is named by its terminal or rule:
  // "Expecting STRING." says more than "Syntax error".
  if (w.expected.empty()) w.expected = named_below(grammar, d.element);
  return w;
}

}  // namespace

// Nothing was consumed: there is no found text.
std::string DefaultParserErrorMessageProvider::buildInsertedMessage(
    const Grammar& grammar, const ParseDiagnostic& diagnostic,
    std::string_view text) const {
  return wording(grammar, diagnostic, text).expecting();
}

// Text skipped to carry on: what was found, then what was expected, unless
// they are the same. Skipped blanks at a line start are its indentation.
std::string DefaultParserErrorMessageProvider::buildDeletedMessage(
    const Grammar& grammar, const ParseDiagnostic& diagnostic,
    std::string_view text) const {
  if (diagnostic.mixedIndent)
    return "Unexpected indentation: this line mixes tabs and spaces "
           "differently from the lines above.";
  if (in_text(text, diagnostic) && starts_indentation(text, diagnostic) &&
      diagnostic.end <= blanks_end(text, diagnostic.begin))
    return "Unexpected indentation.";
  return wording(grammar, diagnostic, text).opposed(/*foundFirst=*/true);
}

std::string DefaultParserErrorMessageProvider::buildMismatchTokenMessage(
    const Grammar& grammar, const ParseDiagnostic& diagnostic,
    std::string_view text) const {
  return wording(grammar, diagnostic, text).opposed(/*foundFirst=*/false);
}

// A frame stops where it has no more text it can place: at the end of the
// document, or where an enclosing frame resumes, so "end of input" before a
// `}` would be wrong.
std::string DefaultParserErrorMessageProvider::buildIncompleteMessage(
    const Grammar& grammar, const ParseDiagnostic& diagnostic,
    std::string_view text) const {
  const Wording w = wording(grammar, diagnostic, text);
  if (diagnostic.end >= text.size())
    return w.expected.empty()
               ? "Unexpected end of input."
               : "Expecting " + w.expected + " but found end of input.";
  return w.opposed(/*foundFirst=*/false);
}

std::string DefaultParserErrorMessageProvider::buildBoundedMessage(
    const Grammar&, const ParseDiagnostic&, std::string_view) const {
  return "This span was decided by the parser's work bound.";
}

std::string DefaultParserErrorMessageProvider::buildNotAllInputParsedMessage(
    std::string_view, TextOffset) const {
  return "Extraneous input after the end of the document.";
}

std::string DefaultParserErrorMessageProvider::buildUnparsedMessage(
    std::string_view) const {
  return "Syntax error: the document could not be parsed.";
}

std::string DefaultParserErrorMessageProvider::buildNoTreeMessage(
    std::string_view) const {
  return "Syntax error: the document is too damaged to build a tree.";
}

}  // namespace cursiv
