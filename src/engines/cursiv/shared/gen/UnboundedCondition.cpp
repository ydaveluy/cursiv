#include "cursiv/shared/gen/UnboundedCondition.hpp"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/GapReach.hpp"
#include "cursiv/shared/gen/LowerPools.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/GapLeaf.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/utils/GrammarPrinter.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/ctstring.hpp"

namespace cursiv::engine {

namespace {

// Whether a round of `node` may read a byte; a callee is entered once.
bool consumes(const LowerPools& c, std::uint16_t node) {
  std::vector<std::uint16_t> calls;
  for (std::vector todo{node}; !todo.empty();) {
    const LexNode& e = c.lex[todo.back()];
    todo.pop_back();
    switch (e.kind) {
      case LexKind::token:
        todo.push_back(lex_token_body(c, e));
        break;
      case LexKind::mark:
        todo.push_back(e.a);
        break;
      case LexKind::call:
        if (const std::uint16_t callee = lex_callee(c, e);
            std::ranges::find(calls, callee) == calls.end()) {
          calls.push_back(callee);
          todo.push_back(callee);
        }
        break;
      case LexKind::chars:
      case LexKind::same:
      case LexKind::through_lit:
      case LexKind::through_same:
      case LexKind::through_nested:
        return true;
      case LexKind::seq:
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i) todo.push_back(c.lexChild[e.a + i]);
        break;
      case LexKind::rep:
        if (e.max != 0) todo.push_back(e.a);
        break;
      default:
        break;
    }
  }
  return false;
}

// The repetitions under `node` that read on without end, and whether the
// parse keeps their rounds: where `free`, no repetition holds `node`.
void endless_runs(const LowerPools& c, std::uint16_t node, bool free,
                  std::vector<std::pair<std::uint16_t, bool>>& out) {
  using enum LexKind;
  for (std::vector todo{std::pair{node, free}}; !todo.empty();) {
    const auto [at, unheld] = todo.back();
    todo.pop_back();
    const LexNode& e = c.lex[at];
    if (e.kind == positive || e.kind == negative) {
      todo.emplace_back(e.a, true);
    } else if (e.kind == rep) {
      // A kept run's rounds read its item from each token: a run inside it is
      // read to its end each time.
      const bool endless = e.max == kEndless && consumes(c, e.a);
      const bool kept = unheld && phrase_kept_run(c, at);
      if (endless) out.emplace_back(at, kept);
      if (!endless || kept) todo.emplace_back(e.a, false);
    } else if (e.kind == seq || e.kind == alt) {
      for (unsigned i = e.b; i-- > 0;)
        todo.emplace_back(c.lexChild[e.a + i], unheld);
    }
  }
}

// A class as the grammar's dump spells it, a single byte quoted.
std::string spell_set(const ByteSet& bytes) {
  CharSet set;
  char last = 0;
  for (unsigned b = 0; b < 256; ++b)
    if (bytes.has(static_cast<unsigned char>(b))) {
      set.set(static_cast<unsigned char>(b));
      last = static_cast<char>(b);
    }
  std::string out;
  if (set.count() != 1)
    charset(out, set);
  else
    out = quote(std::string_view(&last, 1));
  return out;
}

// A run as the warning spells it, `...` for what it does not spell. With
// `identity`, it spells all it reads, so two runs spelt the same read the same.
Descent<std::string> spell_walk(const LowerPools& c, std::uint16_t node,
                                bool identity) {
  const LexNode& e = c.lex[node];
  switch (e.kind) {
    case LexKind::token:
      co_return co_await spell_walk(c, lex_token_body(c, e), identity) +
          (identity ? "#" + decimal(e.a) : "");
    case LexKind::positive:
      co_return "&" + co_await spell_walk(c, e.a, identity);
    case LexKind::negative:
      co_return "!" + co_await spell_walk(c, e.a, identity);
    case LexKind::end_mark:
      co_return "eof";
    case LexKind::chars:
      co_return spell_set(c.sets[e.a]);
    case LexKind::nguard: {
      std::string out = "!";
      for (std::uint32_t k = 0; k < e.min; ++k)
        out += spell_set(c.sets[c.lexChild[e.b + k]]);
      if (identity) {
        if (e.a != kNoSet) out += "/" + spell_set(c.sets[e.a]);
        if (e.max != 0) out += "/scalar";
      }
      co_return out;
    }
    case LexKind::skip: {
      if (!identity) co_return "...";
      std::string out = "~(";
      for (unsigned i = 0; i < e.b; ++i) {
        if (i != 0) out += " ";
        out += co_await spell_walk(c, c.lexChild[e.a + i], identity);
      }
      co_return out + ")";
    }
    case LexKind::seq:
    case LexKind::alt: {
      std::string out;
      unsigned parts = 0;
      for (unsigned i = 0; i < e.b; ++i) {
        const std::uint16_t child = c.lexChild[e.a + i];
        if (!identity && c.lex[child].kind == LexKind::skip) continue;
        if (parts++ != 0) out += e.kind == LexKind::seq ? " " : " | ";
        out += co_await spell_walk(c, child, identity);
      }
      co_return "(" + out + ")";
    }
    case LexKind::rep: {
      std::string body = co_await spell_walk(c, e.a, identity);
      if (identity && e.b != kNoLexNode)
        body += co_await spell_walk(c, e.b, identity);
      if (e.max != kEndless)
        co_return body + "{" + decimal(e.min) + "," + decimal(e.max) + "}";
      co_return body + (e.min == 0   ? "*"
                        : e.min == 1 ? "+"
                                     : "{" + decimal(e.min) + ",}");
    }
    default:
      co_return identity ? "...#" + decimal(node) : "...";
  }
}

std::string spell(const LowerPools& c, std::uint16_t node,
                  bool identity = false) {
  return descend(spell_walk(c, node, identity));
}

}  // namespace

void find_unbounded_runs(const LowerPools& c, std::uint16_t cond,
                         std::uint16_t rule,
                         std::vector<UnboundedCondition>& found) {
  std::vector<std::pair<std::uint16_t, bool>> runs;
  for (const std::uint16_t v : GapReach<LowerPools>(c).leaves(cond)) {
    if (!c.gapLeaves[v].reads_tokens()) continue;
    runs.clear();
    endless_runs(c, c.gapLeaves[v].node, true, runs);
    for (const auto [run, kept] : runs) {
      const UnboundedCondition u{rule, run, kept};
      // Two copies of one run are one run to the author.
      const std::string identity = spell(c, run, true);
      if (std::ranges::none_of(found, [&](const UnboundedCondition& f) {
            return f.rule == u.rule && f.kept == u.kept &&
                   spell(c, f.run, true) == identity;
          }))
        found.push_back(u);
    }
  }
}

std::string unbounded_condition_text(const LowerPools& c,
                                     std::string_view rule,
                                     const UnboundedCondition& u) {
  const std::string who = rule.empty() ? "a rule of this grammar "
                                       : "rule `" + std::string(rule) + "` ";
  return who + "has a condition that matches `" + spell(c, u.run) +
         "` with no length limit" +
         (u.kept ? ": code completion then has to re-read every token "
                   "before the cursor"
                 : ": tested at each token of a long run, it reads to the end "
                   "of the run each time, so parsing time grows with the "
                   "square of the run's length, and code completion has to "
                   "re-read every token before the cursor") +
         " -- give the repetition a maximum (`repeat<Min, Max>`), or match it "
         "as one terminal";
}

}  // namespace cursiv::engine
