// A production copied under another left-hand side, its positions a run of
// the pool of their own.
#pragma once

#include <cstdint>

#include "cursiv/lr/gen/LrBuildCfg.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"

namespace cursiv::engine {

inline std::uint16_t lr_copy_prod(LrBuildCfg& c, std::uint16_t k,
                                  std::uint16_t lhs) {
  if (c.nprods == 0xffffu) {
    lower_refuse(c, LowerFail::tooManyProductions);
    return kNoRow;
  }
  const LrLowProd q = c.prods[k];
  const std::uint16_t self = c.nprods++;
  c.prods[self] = q;
  c.prods[self].lhs = lhs;
  c.prods[self].at = c.nslots;
  for (unsigned i = 0; i < q.len; ++i) {
    const LrLowSlot slot = c.slots[q.at + i];
    c.slots[c.nslots + i] = slot;
  }
  c.nslots += q.len;
  return self;
}

}  // namespace cursiv::engine
