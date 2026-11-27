// The terminals of an element's shortest derivation: a least fixed point over
// the declarative grammar alone, kept apart from the engines on purpose.
#pragma once

#include <algorithm>
#include <compare>
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
  struct Minimum {
    unsigned tokens = kGrammarUnreachable;
    bool conditional = true;
    auto operator<=>(const Minimum&) const = default;
  };

  GrammarCost(const GrammarModel& g, const GrammarActions& act)
      : cost(g.positionCount()) {
    for (bool moved = true; moved;) {
      moved = false;
      for (ElementId e = 0; e < g.positionCount(); ++e) {
        const Minimum c = of(g, act, e);
        if (c < cost[e]) {
          cost[e] = c;
          moved = true;
        }
      }
    }
  }

  [[nodiscard]] Minimum operator[](ElementId e) const { return cost[e]; }

 private:
  std::vector<Minimum> cost;
  [[nodiscard]] Minimum of(const GrammarModel& g, const GrammarActions& act,
                           ElementId e) const {
    const std::span<const ElementId> children = g.childrenOf(e);
    switch (g.kindOf(e)) {
      // A climb costs one operand: the model gives it no children, and its
      // shortest derivation is its primary with no operator at all.
      case ElementKind::infix_rule:
        return act.primary(e) == kNoElement ? Minimum{} : cost[act.primary(e)];
      case ElementKind::literal:
      case ElementKind::character_range:
      case ElementKind::any_character:
      case ElementKind::terminal_rule:
      case ElementKind::through:
        return {1, false};
      case ElementKind::and_predicate:
      case ElementKind::not_predicate:
      case ElementKind::empty_gap:
        return {0, true};
      case ElementKind::ordered_choice:
      case ElementKind::unordered_choice: {
        Minimum best;
        for (const ElementId c : children) best = std::min(cost[c], best);
        return best;
      }
      case ElementKind::repetition: {
        const unsigned n = g.minOf(e);
        if (n == 0 || children.empty()) return {0, false};
        const Minimum head = cost[children[0]];
        return {sat(std::uint64_t{head.tokens} * n), head.conditional};
      }
      default:
        break;
    }
    // A rule, a group, a directive: the sum of what it holds.
    Minimum s{0, false};
    for (const ElementId c : children) {
      s.tokens = sat(s.tokens + cost[c].tokens);
      s.conditional |= cost[c].conditional;
    }
    return s;
  }

  [[nodiscard]] static unsigned sat(std::uint64_t v) {
    return v > kGrammarUnreachable ? kGrammarUnreachable
                                   : static_cast<unsigned>(v);
  }
};

}  // namespace cursiv::fuzzer::detail
