// What the construction knows about one terminal: how it lexes, how it is
// spelled, and the element row a record of it carries.
#pragma once

#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"

namespace cursiv {

inline constexpr std::uint16_t kNoSet = 0xffff;
// How the parse lays a terminal no lexer reads.
inline constexpr std::uint8_t kLaidIndent = 1, kLaidDedent = 2,
                              kLaidImplied = 3;
// A reading's category: a pattern 0, a literal, a keyword.
inline constexpr std::uint8_t kTierLiteral = 1, kTierKeyword = 2;

struct TermInfo {
  // How it lexes, its guard and its documentation stand in the terminal's
  // build row: construction reads them, a parse never does.
  std::uint16_t nameOff = 0, nameLen = 0;
  // The row a record of this terminal writes, and what a repair that invents or
  // re-reads it reports.
  ElementId element = kNoElement;
  bool trivia = false;
  // Trivia a `hidden` rule spells: the parse does not see it either, but a tree
  // keeps it -- what `ignore` spells is dropped.
  bool recorded = false;
  // Its body is one literal, so its bytes are known and insertable. A named
  // terminal is spelled too: `textOff`, not `nameOff`, holds those bytes,
  // since only an anonymous literal is named after its own text.
  bool spelled = false;
  std::uint16_t textOff = 0, textLen = 0;
  // +1 opens a bracket the indentation is blind inside of, -1 closes one.
  std::int8_t delim = 0;
  // The statement's newline: inside a bracket it is a blank.
  bool joins = false;
  // Laid by the parse, never lexed (`kLaid*`); an implied token stands for a
  // refused one where condition `cond` holds.
  std::uint8_t laid = 0;
  std::uint16_t cond = 0;
  // Refuses words behind its head: a match stands only where the table's
  // matcher of them spells none in the token.
  bool refuses = false;

  constexpr bool operator==(const TermInfo&) const = default;
};

// What an election weighs of a category: a literal and a keyword tie, over a
// pattern.
constexpr unsigned lex_weight(unsigned tier) { return tier != 0 ? 1u : 0u; }
// What the lexer elects by among the readings of one match: a visible terminal
// over trivia, then the weight of the branch that read it.
constexpr unsigned lex_rank(unsigned tier, bool trivia) {
  return ((trivia ? 0u : 1u) << 1) | lex_weight(tier);
}
// Terminal `a` of rank `ra` is elected over `b` of rank `rb`: the higher rank,
// then the one declared first.
constexpr bool lex_term_before(unsigned ra, unsigned a, unsigned rb,
                               unsigned b) {
  return ra != rb ? ra > rb : a < b;
}
// ...for terminal `a` read at tier `ta` and `b` at tier `tb`.
constexpr bool lex_term_before(const TermInfo& a, unsigned ta, unsigned ia,
                               const TermInfo& b, unsigned tb, unsigned ib) {
  return lex_term_before(lex_rank(ta, a.trivia), ia, lex_rank(tb, b.trivia),
                         ib);
}

}  // namespace cursiv
