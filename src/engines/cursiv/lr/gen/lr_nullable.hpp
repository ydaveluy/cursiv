// The nonterminals a set of productions derives, and those that may derive
// nothing.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/lr/gen/LrBuildCfg.hpp"
#include "cursiv/shared/table/Symbol.hpp"

namespace cursiv::engine {

// The nonterminals that derive a text through the productions `admit` takes
// alone: a least fixpoint.
template <class Admit>
std::vector<std::uint8_t> lr_derives(const LrBuildCfg& g, Admit admit) {
  std::vector<std::uint8_t> derives(g.nnts, 0);
  // Per production, the nonterminals not yet known to derive; per rule, the
  // productions that read it, once per place.
  std::vector<std::uint32_t> left(g.nprods, 0);
  std::vector<std::vector<unsigned>> readers(g.nnts);
  std::vector<std::uint16_t> found;
  const auto mark = [&](std::uint16_t rule) {
    if (derives[rule] != 0) return;
    derives[rule] = 1;
    found.push_back(rule);
  };
  for (unsigned p = 0; p < g.nprods; ++p) {
    if (!admit(p)) continue;
    g.each_nt(g.prods[p], [&](unsigned, std::uint16_t nt) {
      ++left[p];
      readers[nt].push_back(p);
    });
    if (left[p] == 0) mark(g.prods[p].lhs);
  }
  while (!found.empty()) {
    const std::uint16_t rule = found.back();
    found.pop_back();
    for (const unsigned p : readers[rule])
      if (--left[p] == 0) mark(g.prods[p].lhs);
  }
  return derives;
}

// The nonterminals that may derive nothing. `beforeToken` leaves out the
// productions that hold only at end of input.
inline std::vector<std::uint8_t> lr_nullable(const LrBuildCfg& g,
                                             bool beforeToken) {
  return lr_derives(g, [&](unsigned p) {
    const LrLowProd& q = g.prods[p];
    if (beforeToken && q.endOnly) return false;
    for (unsigned i = 0; i < q.len; ++i)
      if (is_term(g.sym_at(q, i))) return false;
    return true;
  });
}

}  // namespace cursiv::engine
