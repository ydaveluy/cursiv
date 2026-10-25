// Conditions become native Boolean or three-valued expressions in both engines.
#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/engines/shared/table/GapLeaf.hpp"

namespace cursiv {

// Whether the token `sym` is one of `terms`.
template <class Cfg>
std::string gap_terms_expression(const Cfg& cfg, const DynSet& terms,
                                 std::string_view sym) {
  std::string out;
  for (unsigned t = 0; t < cfg.nterms; ++t)
    if (terms.test(t))
      out += std::format("{}{} == {}u", out.empty() ? "" : " || ", sym, t);
  return out.empty() ? "false" : "(" + out + ")";
}

// ...the terminals a cut names.
template <class Cfg>
std::string gap_cut_expression(const Cfg& cfg, const GapLeaf& leaf,
                               std::string_view sym) {
  return gap_terms_expression(cfg, gap_cut_terms(cfg, leaf), sym);
}

template <class Cfg>
class GapConditionEmitter {
 public:
  explicit GapConditionEmitter(const Cfg& cfg) : _cfg(cfg) {}

  template <class Leaf>
  std::string source(unsigned condition, Leaf&& leaf, bool partial) const {
    std::vector<std::string> stack;
    for (std::uint32_t i = _cfg.gapCondAt[condition];
         i < _cfg.gapCondAt[condition + 1u]; ++i) {
      const auto op = _cfg.gapOps[i];
      if (op != kGapNot && op != kGapOr && op != kGapAnd) {
        stack.push_back(leaf(op));
        continue;
      }
      const unsigned count = _cfg.gapOps[++i];
      if (count > stack.size() || (op == kGapNot && count != 1))
        throw std::logic_error("cursiv: invalid condition operands");
      std::string expression;
      if (op == kGapNot) {
        expression = (partial ? "(2u - (" : "!(") + stack.back() +
                     (partial ? "))" : ")");
      } else if (count == 0) {
        expression = op == kGapAnd ? (partial ? "2u" : "true")
                                   : (partial ? "0u" : "false");
      } else {
        expression =
            partial ? (op == kGapOr ? "::std::max({" : "::std::min({") : "(";
        for (std::size_t j = stack.size() - count; j < stack.size(); ++j) {
          if (j != stack.size() - count)
            expression += partial ? ", " : (op == kGapOr ? " || " : " && ");
          expression += stack[j];
        }
        expression += partial ? "})" : ")";
      }
      stack.resize(stack.size() - count);
      stack.push_back(std::move(expression));
    }
    if (stack.empty()) return partial ? "0u" : "false";
    if (stack.size() != 1)
      throw std::logic_error("cursiv: invalid condition result");
    return std::move(stack.back());
  }

 private:
  const Cfg& _cfg;
};

}  // namespace cursiv
