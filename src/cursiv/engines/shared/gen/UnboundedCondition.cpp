#include "cursiv/engines/shared/gen/UnboundedCondition.hpp"

#include <algorithm>
#include <string_view>

#include "cursiv/core/grammar/GrammarPrinter.hpp"
#include "cursiv/core/utils/ctstring.hpp"
#include "cursiv/engines/shared/gen/GapReach.hpp"
#include "cursiv/engines/shared/gen/LowerPools.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

namespace {

// Whether a round of `node` may read a byte.
bool consumes(const LowerPools& c, std::uint16_t node) {
  const LexNode& e = c.lex[node];
  switch (e.kind) {
    case LexKind::token:
      return consumes(c, e.b);
    case LexKind::chars:
      return true;
    case LexKind::seq:
    case LexKind::alt:
      for (unsigned i = 0; i < e.b; ++i)
        if (consumes(c, c.lexKid[e.a + i])) return true;
      return false;
    case LexKind::rep:
      return e.max != 0 && consumes(c, e.a);
    default:
      return false;
  }
}

// The repetitions under `node` that read on without end. A whole terminal
// ends its own run at its own length: a completion counts it as one token.
void endless_runs(const LowerPools& c, std::uint16_t node,
                  std::vector<std::uint16_t>& out) {
  const LexNode& e = c.lex[node];
  if (e.kind == LexKind::positive || e.kind == LexKind::negative) {
    endless_runs(c, e.a, out);
  } else if (e.kind == LexKind::rep) {
    if (e.max == kEndless && consumes(c, e.a))
      out.push_back(node);
    else
      endless_runs(c, e.a, out);
  } else if (e.kind == LexKind::seq || e.kind == LexKind::alt) {
    for (unsigned i = 0; i < e.b; ++i)
      endless_runs(c, c.lexKid[e.a + i], out);
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
    detail::charset(out, set);
  else
    detail::quote(out, std::string(1, last));
  return out;
}

std::string spell(const LowerPools& c, std::uint16_t node) {
  const LexNode& e = c.lex[node];
  switch (e.kind) {
    case LexKind::token:
      return spell(c, e.b);
    case LexKind::positive:
      return "&" + spell(c, e.a);
    case LexKind::negative:
      return "!" + spell(c, e.a);
    case LexKind::end_mark:
      return "eof";
    case LexKind::chars:
      return spell_set(c.sets[e.a]);
    case LexKind::nguard: {
      std::string out = "!";
      for (std::uint32_t k = 0; k < e.min; ++k)
        out += spell_set(c.sets[c.lexKid[e.b + k]]);
      return out;
    }
    case LexKind::seq:
    case LexKind::alt: {
      std::string out;
      unsigned parts = 0;
      for (unsigned i = 0; i < e.b; ++i) {
        const std::uint16_t kid = c.lexKid[e.a + i];
        if (c.lex[kid].kind == LexKind::skip) continue;
        if (parts++ != 0) out += e.kind == LexKind::seq ? " " : " | ";
        out += spell(c, kid);
      }
      return "(" + out + ")";
    }
    case LexKind::rep: {
      const std::string body = spell(c, e.a);
      if (e.max != kEndless)
        return body + "{" + detail::decimal(e.min) + "," +
               detail::decimal(e.max) + "}";
      return body + (e.min == 0   ? "*"
                     : e.min == 1 ? "+"
                                  : "{" + detail::decimal(e.min) + ",}");
    }
    default:
      return "...";
  }
}

}  // namespace

void find_unbounded_runs(const LowerPools& c, std::uint16_t cond,
                         std::uint16_t rule,
                         std::vector<UnboundedCondition>& found) {
  std::vector<std::uint16_t> runs;
  for (const std::uint16_t v : GapReach<LowerPools>(c).leaves(cond)) {
    if (!c.gapLeaves[v].reads_tokens()) continue;
    runs.clear();
    endless_runs(c, c.gapLeaves[v].node, runs);
    for (const std::uint16_t run : runs) {
      const UnboundedCondition u{rule, run};
      // Two copies of one spelling are one run to the author.
      if (std::ranges::none_of(found, [&](const UnboundedCondition& f) {
            return f.rule == u.rule && spell(c, f.run) == spell(c, run);
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
  return who + "asks a condition of the token in hand that reads `" +
         spell(c, u.run) +
         "` to any length: a completion then replays every token before the "
         "caret, one by one -- bound the run (`repeat<0, N>`)";
}

}  // namespace cursiv
