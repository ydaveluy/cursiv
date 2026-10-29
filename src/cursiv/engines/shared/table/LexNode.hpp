// One node of a lexical graph, reified: a terminal's body, a gap's expression
// or a condition's phrase. The automata and the emitters read these graphs and
// nothing of the grammar's types.
#pragma once

#include <cstdint>

namespace cursiv {

// A `seq` whose `min` is one spells a single literal, its guard included.
// `rep` reads `a` from `min` to `max` times, the skip `b` before each time
// past the first, `kNoLexNode` where none.
// `through_lit` runs up to the first literal closer: `a` is the opener's
// node, the kids from `b` a table of classes, one row per byte of the
// closer, `min` the closer's length; `max` is one where the end of the text
// closes it too, and the kid past the table is then that refusal of any byte.
// `nguard` is a zero-width refusal of a word: the kids from `b` are its
// classes, `min` their count, and `a` the class that, following the word,
// means it was not spelled after all -- `kNoSet` when nothing rescues it. A
// `max` of one makes the word the rest of a scalar whose lead byte the node
// before it consumed: the lead stands alone only where the word does not
// follow.
// `indent` and `dedent` are a terminal's whole body, and `implied` the body
// of an implied token: answered by the parse and not by the automaton.
// `mark` names the run under it (`a`) as the token's mark, `same` echoes it
// where it stands, and `through_same` runs up to a closer that echoes it: `a`
// the opener, the two kids from `b` the closer's literal before and after
// the echo, `kNoLexNode` where it has none, and where `min` is three, the run
// between the first and the echo. `through_nested` runs up to the closer at
// depth zero: `a` the opener, the two kids from `b` the opener and the
// closer as literals the search counts, or choices of them; `max` is one
// where the end of the text closes it too.
// `call` reads the body of terminal `b`; an internal call, whose `b` is
// `kNoLexNode`, reads the node `a`.
// A condition's phrase reads tokens: `token` reads terminal `a` whole, its
// body the node `termBuild[a].lex`; `skip` reads trivia, its kids from `a`,
// `b` of them, `token`s, until the token in hand is none of them; `positive`
// and `negative` assert `a` and consume nothing; `gap_has` and `gap_is` ask
// the gap before the token whether it holds or is the expression `a`;
// `end_mark` holds at the end of the text. The lexer mode a token, choice,
// repetition or skip reads in is the configuration's `lexMode`.
enum class LexKind : std::uint8_t {
  chars,
  seq,
  alt,
  rep,
  through_lit,
  nguard,
  mark,
  same,
  through_same,
  through_nested,
  indent,
  dedent,
  implied,
  skip,
  token,
  positive,
  negative,
  gap_has,
  gap_is,
  end_mark,
  call
};

inline constexpr std::uint16_t kNoLexNode = 0xffffu;

// The `max` of a repetition that never ends.
inline constexpr std::uint32_t kEndless = 0xffffffffu;

struct LexNode {
  LexKind kind{};
  std::uint16_t a = 0;  // charset / kid base / kid
  std::uint16_t b = 0;  // kid count / closer base
  std::uint32_t min = 0;
  std::uint32_t max = 0;

  constexpr bool operator==(const LexNode&) const = default;
};

// The node a `call` reads.
template <class Cfg>
constexpr std::uint16_t lex_callee(const Cfg& g, const LexNode& e) {
  return e.b == kNoLexNode ? e.a : g.termBuild[e.b].lex;
}

// The node holding the body of the terminal a `token` reads.
template <class Cfg>
constexpr std::uint16_t lex_token_body(const Cfg& g, const LexNode& e) {
  return g.termBuild[e.a].lex;
}

}  // namespace cursiv
