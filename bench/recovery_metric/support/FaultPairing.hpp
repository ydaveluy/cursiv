// Faults and diagnostics paired one to one. A diagnostic may answer a fault
// whose window it reaches; the pairing answers the most faults, and tries the
// diagnostics nearest a fault first. What is left on either side is a miss or
// a parasite, so one diagnostic more never lowers the parasites.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "recovery_metric/support/FaultWindow.hpp"

namespace cursiv::recovery_metric {

struct FaultPairing {
  static constexpr std::uint32_t kNone = ~std::uint32_t{0};

  // Per fault, the diagnostic that answers it.
  std::vector<std::uint32_t> answer;
  std::uint32_t found = 0, missed = 0, parasites = 0;

  // How far a diagnostic stands from a closed stretch, or `kNone` when it
  // does not reach it. An abandonment names a construct by its two ends --
  // where it opened, where the reading stopped -- and reaches a fault only
  // there: a wide one earns nothing for the text it covers.
  [[nodiscard]] static std::uint32_t reach(const ParseDiagnostic& d,
                                           TextOffset lo, TextOffset hi,
                                           TextOffset at, TextOffset end) {
    const auto gap = [&](TextOffset a, TextOffset b) -> std::uint32_t {
      if (a > hi || b < lo) return kNone;
      return b < at ? at - b : a > end ? a - end : 0;
    };
    if (d.kind != DiagnosticKind::incomplete) return gap(d.begin, d.end);
    return std::min(gap(d.begin, d.begin), gap(d.end, d.end));
  }

  [[nodiscard]] static FaultPairing pair(
      std::span<const FaultWindow> windows,
      std::span<const ParseDiagnostic> diagnostics) {
    FaultPairing p;
    p.answer.assign(windows.size(), kNone);
    std::vector<std::vector<std::uint32_t>> within(windows.size());
    for (std::size_t f = 0; f < windows.size(); ++f) {
      const FaultWindow& w = windows[f];
      std::vector<std::pair<std::uint32_t, std::uint32_t>> near;
      for (std::size_t d = 0; d < diagnostics.size(); ++d) {
        const std::uint32_t gap = reach(diagnostics[d], w.lo, w.hi, w.at, w.end);
        if (gap != kNone) near.emplace_back(gap, static_cast<std::uint32_t>(d));
      }
      std::sort(near.begin(), near.end());
      for (const auto& [gap, d] : near) within[f].push_back(d);
    }
    std::vector<std::uint32_t> owner(diagnostics.size(), kNone);
    std::vector<std::uint8_t> seen(diagnostics.size());
    // Augmenting paths: a fault takes a free diagnostic, or one whose owner
    // can move to another.
    const auto grab = [&](auto& self, std::uint32_t f) -> bool {
      for (const std::uint32_t d : within[f]) {
        if (seen[d] != 0) continue;
        seen[d] = 1;
        if (owner[d] == kNone || self(self, owner[d])) {
          owner[d] = f;
          p.answer[f] = d;
          return true;
        }
      }
      return false;
    };
    for (std::uint32_t f = 0; f < windows.size(); ++f) {
      std::fill(seen.begin(), seen.end(), std::uint8_t{0});
      if (grab(grab, f)) ++p.found;
    }
    p.missed = static_cast<std::uint32_t>(windows.size()) - p.found;
    p.parasites = static_cast<std::uint32_t>(diagnostics.size()) - p.found;
    return p;
  }
};

}  // namespace cursiv::recovery_metric
