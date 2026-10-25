// The nonterminals that may derive nothing.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

// `beforeToken` leaves out the productions that hold only at end of input.
inline std::vector<std::uint8_t> lr_nullable(const LrBuildCfg& g,
                                             bool beforeToken,
                                             bool impliedNullable = false) {
  std::vector<std::uint8_t> nullable(g.nnts, 0);
  detail::settle_empty([&](auto mark) {
    for (unsigned p = 0; p < g.nprods; ++p) {
      const LrLowProd& q = g.prods[p];
      if (nullable[q.lhs] || (beforeToken && g.prodBuild[p].endOnly)) continue;
      bool empty = true;
      for (unsigned i = 0; i < q.len && empty; ++i) {
        const std::uint16_t y = g.sym_at(q, i);
        empty = is_term(y) ? impliedNullable && g.terms[y].laid == kLaidImplied
                           : nullable[nt_index(y)] != 0;
      }
      mark(nullable[q.lhs], empty);
    }
  });
  return nullable;
}

}  // namespace cursiv
