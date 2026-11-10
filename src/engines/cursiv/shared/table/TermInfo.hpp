// What the construction knows about one terminal: how it lexes, how it is
// spelled, and the element row a record of it carries.
#pragma once

#include <cstdint>

#include "cursiv/grammar/ElementId.hpp"

namespace cursiv::engine {

inline constexpr std::uint16_t kNoSet = 0xffff;
// The terminals the parse produces itself, which no lexer reads.
inline constexpr std::uint8_t kSyntheticIndent = 1, kSyntheticDedent = 2,
                              kSyntheticImplied = 3;
// A reading's priority: 0 for a pattern, then a literal and a keyword.
inline constexpr std::uint8_t kLiteralPriority = 1, kKeywordPriority = 2;

struct TermInfo {
  // How it lexes, its guard and its documentation are in the terminal's build
  // row, which only the construction reads.
  std::uint16_t nameOff = 0, nameLen = 0;
  // The row a record of this terminal writes, and what a repair that invents or
  // re-reads it reports.
  ElementId element = kNoElement;
  bool trivia = false;
  // Trivia of a `hidden` rule: the parse skips it but the tree keeps it, unlike
  // `ignored` trivia.
  bool recorded = false;
  // Its body is one literal, so a repair can insert its bytes. They are at
  // `textOff`: only an anonymous literal's name is its text.
  bool spelled = false;
  std::uint16_t textOff = 0, textLen = 0;
  // +1 opens a bracket inside which indentation is ignored, -1 closes one.
  std::int8_t delim = 0;
  // The statement's newline, a blank inside a bracket.
  bool joins = false;
  // Produced by the parse, never lexed (`kSynthetic*`); an implied token
  // replaces a refused one where condition `cond` holds.
  std::uint8_t synthetic = 0;
  std::uint16_t cond = 0;
  // Refuses words after its head: a match holds only where the table's matcher
  // finds none of them in the token.
  bool refuses = false;

  constexpr bool operator==(const TermInfo&) const = default;
};

// Whether any terminal of `C` is recorded: where none is, a parse never fills
// `pending`.
template <const auto& C>
inline constexpr bool kTermsRecorded = [] {
  for (unsigned t = 0; t < C.nterms; ++t)
    if (C.terms[t].recorded) return true;
  return false;
}();

// The weight of a priority when the lexer picks a reading: a literal and a
// keyword tie, above a pattern.
constexpr unsigned lex_weight(unsigned priority) {
  return priority != 0 ? 1u : 0u;
}
// How the lexer ranks the readings of one match: a visible terminal before
// trivia, then by the weight of its priority.
constexpr unsigned lex_rank(unsigned priority, bool trivia) {
  return ((trivia ? 0u : 1u) << 1) | lex_weight(priority);
}
// Whether terminal `a` of rank `ra` wins over `b` of rank `rb`: the higher
// rank, then the one declared first.
constexpr bool lex_term_before(unsigned ra, unsigned a, unsigned rb,
                               unsigned b) {
  return ra != rb ? ra > rb : a < b;
}
// ...for terminal `a` read at priority `ta` and `b` at priority `tb`.
constexpr bool lex_term_before(const TermInfo& a, unsigned ta, unsigned ia,
                               const TermInfo& b, unsigned tb, unsigned ib) {
  return lex_term_before(lex_rank(ta, a.trivia), ia, lex_rank(tb, b.trivia),
                         ib);
}

}  // namespace cursiv::engine
