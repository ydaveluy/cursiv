// The nonterminals that may derive nothing.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/lr/gen/LrBuildCfg.hpp"
#include "cursiv/shared/table/Symbol.hpp"

namespace cursiv::engine {

// `beforeToken` leaves out the productions that hold only at end of input.
inline std::vector<std::uint8_t> lr_nullable(const LrBuildCfg& g,
                                             bool beforeToken) {
  std::vector<std::uint8_t> nullable(g.nnts, 0);
  // Per production of rules alone, the places not yet known to read nothing;
  // per rule, the productions that read it, once per place.
  std::vector<std::uint32_t> left(g.nprods, 0);
  std::vector<std::vector<unsigned>> readers(g.nnts);
  std::vector<std::uint16_t> found;
  const auto mark_nullable = [&](std::uint16_t rule) {
    if (nullable[rule] != 0) return;
    nullable[rule] = 1;
    found.push_back(rule);
  };
  for (unsigned p = 0; p < g.nprods; ++p) {
    const LrLowProd& q = g.prods[p];
    if (beforeToken && g.prodBuild[p].endOnly) continue;
    bool readsToken = false;
    for (unsigned i = 0; i < q.len && !readsToken; ++i)
      readsToken = is_term(g.sym_at(q, i));
    if (readsToken) continue;
    left[p] = q.len;
    for (unsigned i = 0; i < q.len; ++i)
      readers[nt_index(g.sym_at(q, i))].push_back(p);
    if (q.len == 0) mark_nullable(q.lhs);
  }
  while (!found.empty()) {
    const std::uint16_t rule = found.back();
    found.pop_back();
    for (const unsigned p : readers[rule])
      if (--left[p] == 0) mark_nullable(g.prods[p].lhs);
  }
  return nullable;
}

}  // namespace cursiv::engine
