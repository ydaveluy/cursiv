// The chains of a built table, back as rows: the rows one entry names, so a
// reader can ask what a record's run states. The chains are the lowering's,
// which construction extends only where a condition joins two positions.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/lr/table/LrFusedRow.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"

namespace cursiv::test {

// What the record holds versus what stands over it: a record's own chain
// fills it, while a minted pair's `site` half is a directive from above
// that writes into the caller instead. The two halves cannot be one list.
struct EntryRuns {
  std::vector<std::uint16_t> held, over;
};

// Below `nchains` an entry is a chain from the lowering, above it the pair
// construction minted: a cons cell whose first half may itself be minted.
inline EntryRuns entry_runs(const ChainPool& w,
                            std::span<const LrFusedRow> fused,
                            std::uint32_t i) {
  const auto chain = [&w](std::uint32_t k) {
    const Chain c = w.chains[k];
    const std::uint16_t* const at = w.acts.data() + c.off;
    return std::vector<std::uint16_t>(at, at + c.count);
  };
  if (i < w.nchains) return {chain(i), {}};
  const LrFusedRow cell = fused[i - w.nchains];
  EntryRuns r = entry_runs(w, fused, cell.node);
  r.held.insert(r.held.end(), r.over.begin(), r.over.end());
  r.over = chain(cell.site);
  return r;
}

}  // namespace cursiv::test
