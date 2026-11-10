// The terminals of an element's shortest derivation: a least fixed point over
// the declarative grammar alone, kept apart from the engines on purpose.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/ElementKind.hpp"
#include "cursiv/GrammarActions.hpp"
#include "cursiv/GrammarModel.hpp"
#include "cursiv/grammar/ElementId.hpp"

namespace cursiv::fuzzer::detail {

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
    const std::span<const ElementId> children = g.childrenOf(e);
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
        for (const ElementId c : children) best = std::min(cost[c], best);
        return best;
      }
      case ElementKind::repetition: {
        const unsigned n = g.minOf(e);
        if (n == 0 || children.empty()) return 0;
        return sat(std::uint64_t{cost[children[0]]} * n);
      }
      default:
        break;
    }
    // A rule, a group, a directive: the sum of what it holds.
    unsigned s = 0;
    for (const ElementId c : children) s = sat(s + cost[c]);
    return s;
  }

  [[nodiscard]] static unsigned sat(std::uint64_t v) {
    return v > kGrammarUnreachable ? kGrammarUnreachable
                                   : static_cast<unsigned>(v);
  }
};

}  // namespace cursiv::fuzzer::detail
