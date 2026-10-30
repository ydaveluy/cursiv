#include "cursiv/core/validation/RepairMessage.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"

namespace cursiv {

// What the grammar accepts where a repair was owed. A repair is owed
// terminals, so their spellings are asked of the grammar directly -- no
// descent through groups and repetitions, and no cycle to guard against.
// The first names the sentence: where it has no spelling of its own the
// others say nothing about it, and its name alone is the better word.
std::vector<std::string_view> expected_spellings(const Grammar& grammar,
                                                 const ParseDiagnostic& d,
                                                 std::size_t limit) {
  std::vector<std::string_view> spellings;
  const auto add = [&](ElementId owed) {
    if (owed == kNoElement) return;
    for (const std::string_view text : grammar.terminalSpellings(owed))
      if (spellings.size() < limit &&
          std::ranges::find(spellings, text) == spellings.end())
        spellings.push_back(text);
  };
  if (limit == 0) return spellings;
  add(d.element);
  if (spellings.empty()) return spellings;
  for (const ElementId owed : d.also) add(owed);
  return spellings;
}

namespace {

// ...and what to call it where its text cannot be guessed.
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

// Where the line `at` stands in starts.
std::size_t line_start(std::string_view text, std::size_t at) {
  while (at > 0 && text[at - 1] != '\n' && text[at - 1] != '\r') --at;
  return at;
}

// The end of the leading blanks from `at`.
std::size_t blanks_end(std::string_view text, std::size_t at) {
  return std::min(text.find_first_not_of(kIndentBlanks, at), text.size());
}

// A deletion that starts in a line's leading blanks and takes some of them,
// or one standing before them where a dropped indent took no byte: it
// refuses the line's indentation, and with it any text it goes on over.
bool starts_indentation(std::string_view text, const ParseDiagnostic& d) {
  const std::size_t head = line_start(text, d.begin);
  return blanks_end(text, head) >= d.begin &&
         blanks_end(text, d.begin) > d.begin;
}

// Whether the line `at` stands in starts with a space where a line above
// starts with a tab, or the reverse: indentation Python refuses too, and the
// one cause a reader cannot see.
bool mixes_tabs_and_spaces(std::string_view text, std::size_t at) {
  const auto kinds = [&text](std::size_t from, std::size_t to) {
    unsigned k = 0;
    for (std::size_t i = from; i < to; ++i)
      k |= text[i] == ' ' ? 1U : text[i] == '\t' ? 2U : 0U;
    return k;
  };
  const std::size_t head = line_start(text, at);
  const unsigned here = kinds(head, blanks_end(text, head));
  unsigned above = 0;
  for (std::size_t b = 0; b < head;) {
    const std::size_t e = blanks_end(text, b);
    // A blank line indents nothing.
    if (e < head && text[e] != '\n' && text[e] != '\r') above |= kinds(b, e);
    const std::size_t next = text.find_first_of("\n\r", e);
    if (next == std::string_view::npos || next >= head) break;
    b = next + 1;
  }
  return ((here & 1U) != 0 && (above & 2U) != 0) ||
         ((here & 2U) != 0 && (above & 1U) != 0);
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

}  // namespace

// The expectation before the finding. A position whose spellings the grammar
// cannot enumerate -- an identifier, a number -- falls back to the element's
// own name, and a position that answers neither keeps the bare sentence.
std::string repair_message(const Grammar& grammar, const ParseDiagnostic& d,
                           std::string_view text) {
  constexpr std::size_t kMaxNamed = 4;
  const bool sane = d.begin <= d.end && d.end <= text.size();
  const std::string_view over =
      sane ? text.substr(d.begin, d.end - d.begin) : std::string_view{};
  const bool atEnd = d.end >= text.size();
  // A skip over pure whitespace -- the only byte a tight region ever refuses
  // -- quotes to nothing: `found` is then empty despite the span itself
  // being non-empty, and what is left to say is the expectation alone.
  const std::string found = quote_found(over);
  // A token the reading could have taken is not what was missing: naming the
  // set it belongs to says the fault twice and denies it once. "Unexpected
  // '+', expecting one of '+', '('." tells an author nothing a bare
  // "Unexpected '+'." did not already.
  const std::vector<std::string_view> named =
      expected_spellings(grammar, d, kMaxNamed);
  const bool echoes =
      !found.empty() &&
      std::ranges::find(named, std::string_view(found)) != named.end();
  std::string expected = echoes ? std::string() : spell_expected(named);
  // A position the grammar cannot spell still has a name at or just below it
  // -- a terminal, a rule -- and naming it beats the bare sentence: "Expecting
  // STRING." tells an author what to write where "Syntax error" does not. The
  // descent is shallow on purpose, like the spellings' own.
  if (expected.empty()) expected = std::string(named_below(grammar, d.element));
  const auto expecting = [&] {
    return expected.empty() ? std::string("Syntax error")
                            : "Expecting " + expected + ".";
  };
  // Three sentences serve the kinds that have text to oppose, and they differ
  // only in which side is named first.
  const auto opposed = [&](bool foundFirst) {
    if (found.empty()) return expecting();
    if (expected.empty()) return "Unexpected '" + found + "'.";
    return foundFirst
               ? "Unexpected '" + found + "', expecting " + expected + "."
               : "Expecting " + expected + " but found '" + found + "'.";
  };

  switch (d.kind) {
    case DiagnosticKind::inserted:
      // Nothing was consumed: there is no "found" to oppose.
      return expecting();
    case DiagnosticKind::replaced:
      return opposed(/*foundFirst=*/false);
    case DiagnosticKind::deleted:
      // Text the parser had to skip to carry on: what was expected is what it
      // skipped towards, so it is named second here -- unless skipping it is
      // what was expected, which is said once, not twice. Skipped blanks
      // quote to nothing, and at a line start they are its indentation. A
      // line indented with tabs and spaces unlike the lines above says so,
      // whatever else the deletion took.
      if (sane && starts_indentation(text, d)) {
        if (mixes_tabs_and_spaces(text, d.begin))
          return "Unexpected indentation: this line mixes tabs and spaces "
                 "differently from the lines above.";
        if (d.end <= blanks_end(text, d.begin))
          return "Unexpected indentation.";
      }
      return opposed(/*foundFirst=*/true);
    case DiagnosticKind::incomplete:
      // A frame runs out of text it can place, and that is not always the end
      // of the document: it also stops where an enclosing frame resumes, and
      // saying "end of input" over a `}` that is right there reads as a lie.
      if (atEnd)
        return expected.empty()
                   ? "Unexpected end of input."
                   : "Expecting " + expected + " but found end of input.";
      return opposed(/*foundFirst=*/false);
    case DiagnosticKind::bounded:
      return "This span was settled by the parser's work bound.";
    case DiagnosticKind::conversion_error:
      break;
  }
  return "Syntax error";
}

}  // namespace cursiv
