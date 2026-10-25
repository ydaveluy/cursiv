#include "cursiv/engines/lr/gen/lr_entry_rows.hpp"

#include "cursiv/engines/shared/gen/chain_run.hpp"

namespace cursiv {

AstEntryTable lr_entry_table(const LrBuildTable& a) {
  AstEntryTable t;
  for (std::uint16_t c = 0; c < a.writes.nchains; ++c) {
    t.runs.push_back(chain_run(a, c));
    t.add(c);
    t.turns.push_back(a.writes.chainBuild[c].turn);
  }
  for (std::uint32_t f = 0; f < a.nfused; ++f) {
    const auto cell = a.fused[f];
    t.add(cell.site, cell.node);
    t.turns.push_back(t.turns[cell.node]);
  }
  t.count = t.chain.size();
  return t;
}

}  // namespace cursiv
