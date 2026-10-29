// A condition that may read the token in hand over a run of terminals with no
// end. A parse reads such a run round by round and keeps each round's answer,
// so tests at each token of a long run read it once, unless it stands inside
// another repetition or a round of it may read no token: each test then reads
// to the run's end, and parsing grows with the square of the run. Either way a
// completion cannot step over what the condition decided, and replays every
// token before the caret.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

struct LowerPools;

struct UnboundedCondition {
  // The rule an author wrote, and the repetition in the lexical pool.
  std::uint16_t rule = kNoRow;
  std::uint16_t run = 0;
  // The parse keeps the run's rounds: only a completion pays for it.
  bool kept = false;

  constexpr bool operator==(const UnboundedCondition&) const = default;
};

// Whether every reading of phrase node `node` moves past a token.
template <class Cfg>
bool phrase_advances(const Cfg& c, std::uint16_t node) {
  const LexNode& e = c.lex[node];
  switch (e.kind) {
    case LexKind::token:
      return true;
    case LexKind::seq:
      for (unsigned i = 0; i < e.b; ++i)
        if (phrase_advances(c, c.lexKid[e.a + i])) return true;
      return false;
    case LexKind::alt:
      for (unsigned i = 0; i < e.b; ++i)
        if (!phrase_advances(c, c.lexKid[e.a + i])) return false;
      return e.b != 0;
    case LexKind::rep:
      return e.min != 0 && e.max != 0 && phrase_advances(c, e.a);
    default:
      return false;
  }
}

// Whether `node` is a run a parse reads round by round, keeping each round's
// answer, where no repetition of its phrase holds it: a repetition with no
// maximum whose every round reads a token.
template <class Cfg>
bool phrase_kept_run(const Cfg& c, std::uint16_t node) {
  const LexNode& e = c.lex[node];
  return e.kind == LexKind::rep && e.max == kEndless && phrase_advances(c, e.a);
}

// Whether a phrase reads a kept run through its sequences and choices alone.
template <class Cfg>
bool phrase_keeps_runs(const Cfg& c, std::uint16_t node) {
  if (const LexNode& e = c.lex[node];
      e.kind == LexKind::seq || e.kind == LexKind::alt) {
    for (unsigned i = 0; i < e.b; ++i)
      if (phrase_keeps_runs(c, c.lexKid[e.a + i])) return true;
    return false;
  }
  return phrase_kept_run(c, node);
}

// The parts of a phrase read one after the other: a sequence's, nested
// sequences flattened, or the phrase alone.
template <class Cfg>
void phrase_parts(const Cfg& c, std::uint16_t node,
                  std::vector<std::uint16_t>& out) {
  const LexNode& e = c.lex[node];
  if (e.kind != LexKind::seq) {
    out.push_back(node);
    return;
  }
  for (unsigned i = 0; i < e.b; ++i) phrase_parts(c, c.lexKid[e.a + i], out);
}

// What condition `cond` reads without end, as `rule` asks it: one finding per
// rule and spelled run.
void find_unbounded_runs(const LowerPools& c, std::uint16_t cond,
                         std::uint16_t rule,
                         std::vector<UnboundedCondition>& found);

// One sentence: the rule and the run named, what it costs, and what to write
// instead. An empty `rule` is a rule of the grammar no name reaches.
std::string unbounded_condition_text(const LowerPools& c,
                                     std::string_view rule,
                                     const UnboundedCondition& u);

}  // namespace cursiv
