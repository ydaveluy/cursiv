// One node of a lexical graph, reified: a terminal's body, a gap's expression
// or a condition's phrase. The automata and the emitters read these graphs and
// nothing of the grammar's types.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

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

// Whether the graphs at `x` and `y` read alike: `same(a, b)` compares two
// nodes' kinds and own fields, a sequence's or choice's width among them;
// their children, and a repetition's body, then compare in turn.
template <class Cfg, class Same>
bool lex_same_graph(const Cfg& g, std::uint16_t x, std::uint16_t y,
                    Same&& same) {
  std::vector<std::pair<std::uint16_t, std::uint16_t>> todo{{x, y}};
  while (!todo.empty()) {
    const auto [p, q] = todo.back();
    todo.pop_back();
    if (p == q) continue;
    const LexNode& a = g.lex[p];
    const LexNode& b = g.lex[q];
    if (!same(a, b)) return false;
    if (a.kind == LexKind::seq || a.kind == LexKind::alt)
      for (unsigned i = 0; i < a.b; ++i)
        todo.emplace_back(g.lexChild[a.a + i], g.lexChild[b.a + i]);
    else if (a.kind == LexKind::rep)
      todo.emplace_back(a.a, b.a);
  }
  return true;
}

}  // namespace cursiv::engine
