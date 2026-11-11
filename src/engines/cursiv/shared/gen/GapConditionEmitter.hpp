// Conditions become native Boolean or three-valued expressions in both engines.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/Concat.hpp"
#include "cursiv/shared/table/GapLeaf.hpp"

namespace cursiv::engine {

// Whether the token `sym` is one of `terms`.
template <class Cfg>
std::string gap_terms_expression(const Cfg& cfg, const DynSet& terms,
                                 std::string_view sym) {
  std::string out;
  for (unsigned t = 0; t < cfg.nterms; ++t)
    if (terms.test(t))
      out += concat(out.empty() ? "" : " || ", sym, " == ", t, "u");
  return out.empty() ? "false" : "(" + out + ")";
}

// ...the terminals a cut names.
template <class Cfg>
std::string gap_cut_expression(const Cfg& cfg, const GapLeaf& leaf,
                               std::string_view sym) {
  return gap_terms_expression(cfg, gap_cut_terms(cfg, leaf), sym);
}

// Condition `condition` of `cfg` as an expression, `leaf(l)` spelling leaf
// `l`: Boolean, or three-valued where `partial`.
template <class Cfg, class Leaf>
std::string gap_condition_source(const Cfg& cfg, unsigned condition,
                                 Leaf&& leaf, bool partial) {
  std::vector<std::string> stack;
  for (std::uint32_t i = cfg.gapCondAt[condition];
       i < cfg.gapCondAt[condition + 1u]; ++i) {
    const auto op = cfg.gapOps[i];
    if (op != kGapNot && op != kGapOr && op != kGapAnd) {
      stack.push_back(leaf(op));
      continue;
    }
    const unsigned count = cfg.gapOps[++i];
    if (count == 0 || count > stack.size() || (op == kGapNot && count != 1))
      throw std::logic_error("cursiv: invalid condition operands");
    std::string expression;
    if (op == kGapNot) {
      expression =
          (partial ? "(2u - (" : "!(") + stack.back() + (partial ? "))" : ")");
    } else if (partial) {
      // Three-valued: an operand is read until one decides, 2 for an OR and
      // 0 for an AND.
      const std::string_view stop = op == kGapOr ? "2u" : "0u";
      const std::string_view keep =
          op == kGapOr ? "::std::max(" : "::std::min(";
      const std::size_t first = stack.size() - count;
      const std::string v = concat("v", i);
      expression = concat("[&] { unsigned ", v, " = ", stack[first], ";");
      for (std::size_t j = first + 1; j < stack.size(); ++j)
        expression += concat(" if (", v, " != ", stop, ") ", v, " = ", keep, v,
                             ", ", stack[j], ");");
      expression += concat(" return ", v, "; }()");
    } else {
      expression = "(";
      for (std::size_t j = stack.size() - count; j < stack.size(); ++j) {
        if (j != stack.size() - count)
          expression += op == kGapOr ? " || " : " && ";
        expression += stack[j];
      }
      expression += ")";
    }
    stack.resize(stack.size() - count);
    stack.push_back(std::move(expression));
  }
  if (stack.empty()) return partial ? "0u" : "false";
  if (stack.size() != 1)
    throw std::logic_error("cursiv: invalid condition result");
  return std::move(stack.back());
}

}  // namespace cursiv::engine
