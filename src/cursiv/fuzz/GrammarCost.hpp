// What the shortest derivation of an element costs, in terminals: a least
// fixed point over the declarative grammar alone. The LR side has its own
// answer (what a state owes); reusing it would pierce the independence this
// lane exists for, the one deliberate exception to the reuse rule.
#pragma once

#include <algorithm>
#include <span>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/fuzz/ElementKind.hpp"
#include "cursiv/fuzz/GrammarActions.hpp"
#include "cursiv/fuzz/GrammarModel.hpp"

namespace cursiv::fuzzer {

inline constexpr unsigned kGrammarUnreachable = 1u << 20;

struct GrammarCost {
  GrammarCost(const GrammarModel& g, const GrammarActions& act)
      : cost(g.positionCount(), kGrammarUnreachable) {
    for (bool moved = true; moved;) {
      moved = false;
      for (ElementId e = 0; e < g.positionCount(); ++e) {
        const unsigned c = of(g, act, e);
        if (c < cost[e]) {
          cost[e] = c;
          moved = true;
        }
      }
    }
  }

  [[nodiscard]] unsigned operator[](ElementId e) const { return cost[e]; }

  std::vector<unsigned> cost;

 private:
  [[nodiscard]] unsigned of(const GrammarModel& g, const GrammarActions& act,
                            ElementId e) const {
    const std::span<const ElementId> kids = g.childrenOf(e);
    switch (g.kindOf(e)) {
      // A climb costs one operand: the model gives it no children, and its
      // shortest derivation is its primary with no operator at all.
      case ElementKind::infix_rule:
        return act.primary(e) == kNoElement ? kGrammarUnreachable
                                            : cost[act.primary(e)];
      case ElementKind::literal:
      case ElementKind::character_range:
      case ElementKind::any_character:
      case ElementKind::terminal_rule:
        return 1;
      case ElementKind::and_predicate:
      case ElementKind::not_predicate:
        return 0;
      case ElementKind::ordered_choice:
      case ElementKind::unordered_choice: {
        unsigned best = kGrammarUnreachable;
        for (const ElementId c : kids) best = std::min(cost[c], best);
        return best;
      }
      case ElementKind::repetition: {
        const unsigned n = g.minOf(e);
        if (n == 0 || kids.empty()) return 0;
        return sat(cost[kids[0]] * n);
      }
      default:
        break;
    }
    // A rule, a group, a directive: the sum of what it holds.
    unsigned s = 0;
    for (const ElementId c : kids) s = sat(s + cost[c]);
    return s;
  }

  [[nodiscard]] static unsigned sat(unsigned v) {
    return v > kGrammarUnreachable ? kGrammarUnreachable : v;
  }
};

}  // namespace cursiv::fuzzer
