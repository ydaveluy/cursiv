// One class of faults summed over its documents. Rates are read off the sums,
// never averaged per document; the diagnostics a fault cost and the parse's
// cost are kept as distributions, since a mean hides the tail.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "recovery_metric/support/RepairVerdict.hpp"

namespace cursiv {

struct RepairTally {
  std::uint32_t docs = 0, faults = 0, diagnostics = 0;
  // Documents set apart: their faults left the twin's tree as it was.
  std::uint32_t noop = 0;
  std::uint32_t found = 0, missed = 0, parasites = 0;
  std::uint32_t silent = 0, refused = 0, treeless = 0;
  std::uint64_t eligible = 0, kept = 0, deleted = 0, abandoned = 0, other = 0;
  std::uint32_t restored = 0, invented = 0;
  std::uint64_t astTotal = 0, astKept = 0, astUnaligned = 0;
  // Documents whose every untouched node survived.
  std::uint32_t astWhole = 0;
  // Documents by diagnostics per fault: none, below one, one, up to two, up
  // to four, beyond.
  std::array<std::uint32_t, 6> perFault{};
  std::vector<double> costs;
  // Per named ceiling, the documents that reached it; `machine` those that
  // reached any, whose answer a limit of the parser may have given.
  std::vector<std::uint32_t> ceilings;
  std::uint32_t machine = 0;

  void add(const RepairVerdict& v) {
    if (v.noop) {
      ++noop;
      return;
    }
    ++docs;
    faults += v.faults;
    diagnostics += v.diagnostics;
    found += v.found;
    missed += v.missed;
    parasites += v.parasites;
    silent += v.silent ? 1 : 0;
    refused += v.accepted ? 0 : 1;
    treeless += v.tree ? 0 : 1;
    eligible += v.text.eligible;
    kept += v.text.kept;
    deleted += v.text.deleted;
    abandoned += v.text.abandoned;
    other += v.text.other;
    restored += v.text.restored;
    invented += v.text.invented;
    astTotal += v.ast.total;
    astKept += v.ast.kept;
    astUnaligned += v.ast.unaligned;
    astWhole += v.ast.kept == v.ast.total ? 1 : 0;
    const double r = v.faults == 0 ? 0.0 : double(v.diagnostics) / v.faults;
    perFault[v.diagnostics == 0 ? 0
             : r < 1.0          ? 1
             : r == 1.0         ? 2
             : r <= 2.0         ? 3
             : r <= 4.0         ? 4
                                : 5]++;
    if (v.cost >= 0) costs.push_back(v.cost);
    if (ceilings.size() < v.ceilings.size()) ceilings.resize(v.ceilings.size());
    bool any = false;
    for (std::size_t i = 0; i < v.ceilings.size(); ++i)
      if (v.ceilings[i] != 0) {
        ++ceilings[i];
        any = true;
      }
    machine += any ? 1 : 0;
  }

  // Nearest rank; negative when nothing was measured.
  [[nodiscard]] double cost(double quantile) const {
    if (costs.empty()) return -1;
    std::vector<double> s = costs;
    std::sort(s.begin(), s.end());
    const auto rank = static_cast<std::size_t>(
        std::ceil(quantile * static_cast<double>(s.size())));
    return s[rank == 0 ? 0 : rank - 1];
  }
};

}  // namespace cursiv
