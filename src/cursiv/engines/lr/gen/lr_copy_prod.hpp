// A production copied under another left-hand side, its positions a run of
// the pool of their own.
#pragma once

#include <cstdint>

#include "cursiv/engines/lr/gen/LrBuildCfg.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"

namespace cursiv {

inline std::uint16_t lr_copy_prod(LrBuildCfg& c, std::uint16_t k,
                                  std::uint16_t lhs) {
  if (c.nprods == 0xffffu) {
    lower_refuse(c, LowerFail::tooManyProductions);
    return kNoRow;
  }
  const LrLowProd q = c.prods[k];
  const LrProdBuild b = c.prodBuild[k];
  const std::uint16_t self = c.nprods++;
  c.prods[self] = q;
  c.prods[self].lhs = lhs;
  c.prods[self].at = c.nslots;
  c.prodBuild[self] = b;
  for (unsigned i = 0; i < q.len; ++i) {
    const LrLowSlot slot = c.slots[q.at + i];
    const std::uint16_t sym = c.slotSym[q.at + i];
    const std::uint8_t cfg = c.slotCfg[q.at + i];
    c.slots[c.nslots + i] = slot;
    c.slotSym[c.nslots + i] = sym;
    c.slotCfg[c.nslots + i] = cfg;
  }
  c.nslots += q.len;
  return self;
}

}  // namespace cursiv
