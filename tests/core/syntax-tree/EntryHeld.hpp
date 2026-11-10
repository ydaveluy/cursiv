// The action rows a table entry names, so a test can ask what a record's run
// holds. Construction adds pairs to the lowering's chains only where a
// condition joins two positions.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/lr/table/LrFusedRow.hpp"
#include "cursiv/shared/gen/ChainPool.hpp"

namespace cursiv::test {

// `held` fills the record itself; `over` is the `site` half of a pair made by
// construction, which writes into the caller. The two cannot be one list.
struct EntryRuns {
  std::vector<std::uint16_t> held, over;
};

// Below `nchains` an entry is a chain from the lowering; above, a pair made by
// construction, whose first half may itself be such a pair.
inline EntryRuns entry_runs(const engine::ChainPool& w,
                            std::span<const engine::LrFusedRow> fused,
                            std::uint32_t i) {
  const auto chain = [&w](std::uint32_t k) {
    const engine::Chain c = w.chains[k];
    const std::uint16_t* const at = w.acts.data() + c.off;
    return std::vector<std::uint16_t>(at, at + c.count);
  };
  if (i < w.nchains) return {chain(i), {}};
  const engine::LrFusedRow cell = fused[i - w.nchains];
  EntryRuns r = entry_runs(w, fused, cell.node);
  r.held.insert(r.held.end(), r.over.begin(), r.over.end());
  r.over = chain(cell.site);
  return r;
}

}  // namespace cursiv::test
