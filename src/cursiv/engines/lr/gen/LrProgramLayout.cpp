// A cell's run of actions is interned once; its index and length share one
// word in the matrix row.
#include "cursiv/engines/lr/gen/LrProgramLayout.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

#include "cursiv/engines/lr/gen/LrGotoRows.hpp"
#include "cursiv/engines/lr/gen/lr_term_run.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"

namespace cursiv {

LrProgramLayout::LrProgramLayout(const LrBuildTable& a) {
  std::size_t maxRun = 1;
  for (unsigned s = 0; s < a.nstates; ++s)
    lr_each_run(a.actions.data() + a.actionBase[s], a.actionCount[s],
                [&](std::size_t i, std::size_t j) {
                  maxRun = std::max(maxRun, j - i);
                });
  countBits = static_cast<unsigned>(std::bit_width(maxRun));
  std::vector<std::uint64_t> row(a.cfg.nterms);
  {
    // The interned cells serve the action rows alone.
    std::map<std::vector<std::uint32_t>, std::uint64_t> cells;
    std::vector<std::uint32_t> key;
    for (unsigned s = 0; s < a.nstates; ++s) {
      std::ranges::fill(row, 0);
      const unsigned b = a.actionBase[s];
      lr_each_run(
          a.actions.data() + b, a.actionCount[s],
          [&](std::size_t i, std::size_t end) {
            const auto term = a.actions[b + i].term;
            key.clear();
            for (std::size_t k = i; k < end; ++k) {
              const LrAction& x = a.actions[b + k];
              key.push_back(lr_op_of(x));
              // A shift's terminal also controls virtual tokens and
              // indentation.
              key.push_back(lr_takes_token(x.kind) ? term : 0u);
              key.push_back(x.exact ? 1u : 0u);
            }
            const auto value =
                (std::uint64_t{ops.size()} << countBits) | (end - i);
            const auto [at, fresh] = cells.try_emplace(key, value);
            if (fresh) {
              if (ops.size() >
                  std::numeric_limits<std::uint32_t>::max() - (end - i))
                throw LowerRefused(LowerFail::tableTooLarge);
              for (std::size_t k = i; k < end; ++k) {
                const LrAction& x = a.actions[b + k];
                if (x.exact) {
                  if (exact.size() <= ops.size() / 64)
                    exact.resize(ops.size() / 64 + 1);
                  exact[ops.size() / 64] |= std::uint64_t{1}
                                            << (ops.size() % 64);
                }
                ops.push_back(lr_op_of(x));
              }
            }
            row[term] = at->second;
          });
      actions.append(row, 0);
    }
  }
  actions.seal();

  // A guarded goto names its program above the states.
  if (a.nstates + a.guards.size() >= 0xffffu)
    throw LowerRefused(LowerFail::tableTooLarge);
  const LrGotoRows& go = a.gotoRows;
  row.resize(a.cfg.nnts);
  for (unsigned s = 0; s < a.nstates; ++s) {
    std::ranges::fill(row, 0xffffu);
    for (const LrGotoRows::Edge e : go[s])
      row[e.nt] = e.guard == 0 ? e.to : a.nstates + e.guard - 1u;
    gotos.append(row, 0xffffu);
  }
  gotos.seal();
}

}  // namespace cursiv
