// One node of a lexical graph, reified: a terminal's body, a gap's expression
// or a condition's phrase. The automata and the emitters read these graphs and
// nothing of the grammar's types.
#pragma once

#include <cstdint>

namespace cursiv::engine {

// The lexer mode a token, choice, repetition or skip reads in is the
// configuration's `lexMode`.
enum class LexKind : std::uint8_t {
  chars,
  // With `min` one: a single literal, its guard included.
  seq,
  // With `min` one: a letter of a case-insensitive literal, its children the
  // cases.
  alt,
  // `a` from `min` to `max` times, the skip `b` before each time but the first.
  rep,
  // Up to the first literal closer: `a` the opener, the children from `b` a
  // class per closer byte, `min` its length; `max` one if the text's end closes
  // it.
  through_lit,
  // A zero-width refusal of a word, its `min` classes the children from `b`;
  // `a` is the class that lifts it. With `max` one, it is a scalar's rest.
  nguard,
  // `mark` names the run `a` as the token's mark, and `same` echoes it.
  mark,
  same,
  // Up to a closer that echoes the mark: `a` the opener, the two children from
  // `b` the closer's literal before and after the echo.
  through_same,
  // Up to the closer at depth zero: `a` the opener, the two children from `b`
  // the opener and closer it counts; `max` one if the text's end closes it.
  through_nested,
  // Bodies the parse decides, not the automaton.
  indent,
  dedent,
  implied,
  // Trivia: the `b` tokens from child `a`, until the token in hand is none.
  skip,
  // Terminal `a`, read whole.
  token,
  // Zero-width assertions of `a`.
  positive,
  negative,
  // Whether the gap before the token holds, or is, the expression `a`.
  gap_has,
  gap_is,
  end_mark,
  // The body of terminal `b`, or the node `a` where `b` is `kNoLexNode`.
  call
};

inline constexpr std::uint16_t kNoLexNode = 0xffffu;

// The `max` of a repetition that never ends.
inline constexpr std::uint32_t kEndless = 0xffffffffu;

struct LexNode {
  LexKind kind{};
  std::uint16_t a = 0;  // charset / child base / child
  std::uint16_t b = 0;  // child count / closer base
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

}  // namespace cursiv::engine
