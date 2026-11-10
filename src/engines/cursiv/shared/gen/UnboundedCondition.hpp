// A condition that may read over a run of terminals with no end. Inside
// another repetition, or where a round may read no token, parsing grows with
// the square of the run; a completion always replays the run.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/WriteKind.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

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
Descent<bool> phrase_advances_walk(const Cfg& c, std::uint16_t node) {
  const LexNode& e = c.lex[node];
  switch (e.kind) {
    case LexKind::token:
      co_return true;
    case LexKind::seq:
      for (unsigned i = 0; i < e.b; ++i)
        if (co_await phrase_advances_walk(c, c.lexChild[e.a + i]))
          co_return true;
      co_return false;
    case LexKind::alt:
      for (unsigned i = 0; i < e.b; ++i)
        if (!co_await phrase_advances_walk(c, c.lexChild[e.a + i]))
          co_return false;
      co_return e.b != 0;
    case LexKind::rep:
      if (e.min == 0 || e.max == 0) co_return false;
      co_return co_await phrase_advances_walk(c, e.a);
    default:
      co_return false;
  }
}
template <class Cfg>
bool phrase_advances(const Cfg& c, std::uint16_t node) {
  return descend(phrase_advances_walk(c, node));
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
  for (std::vector todo{node}; !todo.empty();) {
    const std::uint16_t at = todo.back();
    todo.pop_back();
    if (const LexNode& e = c.lex[at];
        e.kind == LexKind::seq || e.kind == LexKind::alt) {
      for (unsigned i = 0; i < e.b; ++i) todo.push_back(c.lexChild[e.a + i]);
    } else if (phrase_kept_run(c, at)) {
      return true;
    }
  }
  return false;
}

// The parts of a phrase read one after the other: a sequence's, nested
// sequences flattened, or the phrase alone.
template <class Cfg>
void phrase_parts(const Cfg& c, std::uint16_t node,
                  std::vector<std::uint16_t>& out) {
  for (std::vector todo{node}; !todo.empty();) {
    const std::uint16_t at = todo.back();
    todo.pop_back();
    if (const LexNode& e = c.lex[at]; e.kind == LexKind::seq)
      for (unsigned i = e.b; i-- > 0;) todo.push_back(c.lexChild[e.a + i]);
    else
      out.push_back(at);
  }
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

}  // namespace cursiv::engine
