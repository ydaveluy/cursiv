// An engine's record table as the AST emitter reads it: an entry runs its own
// chain after the entry it extends, if any, and only the first `count` are the
// table's. `turns` holds, per entry, whether its row is a turn that reached
// back over its receiver; empty where a turn's record says so by its state.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cursiv {

struct AstEntryTable {
  static constexpr std::uint32_t kNoEntry = 0xFFFFFFFFu;

  std::vector<std::vector<std::uint16_t>> runs;
  std::vector<std::uint32_t> chain;
  std::vector<std::uint32_t> from;
  std::size_t count = 0;
  std::vector<std::uint8_t> turns;
  bool turnsByState = false;

  std::uint32_t add(std::uint32_t own, std::uint32_t extends = kNoEntry) {
    chain.push_back(own);
    from.push_back(extends);
    return static_cast<std::uint32_t>(chain.size() - 1);
  }
  // The chains an entry runs, its base first, without one frame per link.
  std::vector<std::uint32_t> chains(std::uint32_t e) const {
    std::vector<std::uint32_t> out;
    for (; from[e] != kNoEntry; e = from[e]) out.push_back(chain[e]);
    out.push_back(chain[e]);
    std::reverse(out.begin(), out.end());
    return out;
  }
  // What fills the node a record built, and what writes it into the region
  // above: the last chain is the site's, every one before it is held.
  void rows(std::uint32_t e, std::vector<std::uint16_t>& held,
            std::vector<std::uint16_t>& over) const {
    const auto run = chains(e);
    held = runs[run.front()];
    over.clear();
    for (std::size_t k = 1; k + 1 < run.size(); ++k)
      held.insert(held.end(), runs[run[k]].begin(), runs[run[k]].end());
    if (run.size() > 1) over = runs[run.back()];
  }
};

}  // namespace cursiv
