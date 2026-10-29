// How much of the author's text a repaired document still reads in place.
// Every twin token no fault touches is placed in the faulted text by the
// known edits -- an alignment, never a multiset -- and found there as a live
// leaf of the same extent, under a skipped span, under an abandoned
// construct, or as none of these. A window's neighbours count: a separator
// restored keeps them, a value dropped in its place does not. The repair's
// own tokens -- an insertion, a re-read -- restore a token the faults took
// from the twin between the same neighbours, or are invented.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "recovery_metric/support/FaultWindow.hpp"
#include "recovery_metric/support/RepairCase.hpp"

namespace cursiv::recovery_metric {

struct TextKept {
  // Bytes of the twin's tokens no fault touches.
  std::uint64_t eligible = 0, kept = 0, deleted = 0, abandoned = 0, other = 0;
  // Tokens the repair made: those that stand for a token the faults took.
  std::uint32_t restored = 0, invented = 0;

  // `restores(token, owed)`: whether a token the repair owed the grammar
  // spells the twin's token.
  [[nodiscard]] static TextKept of(
      std::span<const CstLeaf> twin, const RepairCase& c,
      std::span<const FaultWindow> windows, std::span<const CstLeaf> got,
      std::span<const ParseDiagnostic> diagnostics,
      function_ref<bool(std::size_t token, ElementId owed)> restores) {
    TextKept t;
    const std::size_t n = twin.size();
    std::vector<std::uint8_t> touched(n);
    for (const FaultWindow& w : windows)
      for (std::uint32_t i = w.coreFirst; i < w.coreLast && i < n; ++i)
        touched[i] = 1;

    const auto covered = [&](const CstLeaf& l, DiagnosticKind kind) {
      for (const ParseDiagnostic& d : diagnostics)
        if (d.kind == kind && d.begin <= l.begin && l.end <= d.end) return true;
      return false;
    };
    std::vector<CstLeaf> live, skipped;
    for (const CstLeaf& l : got) {
      if (l.hidden || l.begin == l.end) continue;
      if (!l.recovered)
        live.push_back(l);
      else if (!covered(l, DiagnosticKind::replaced))
        skipped.push_back(l);
    }
    const auto abandoned = [&](TextOffset b, TextOffset e) {
      for (const ParseDiagnostic& d : diagnostics)
        if (d.kind == DiagnosticKind::incomplete && d.begin <= b && e <= d.end)
          return true;
      return false;
    };
    const auto byBegin = [](const CstLeaf& l, TextOffset at) {
      return l.begin < at;
    };
    for (std::size_t i = 0; i < n; ++i) {
      if (touched[i] != 0) continue;
      const TextOffset width = twin[i].end - twin[i].begin;
      const TextOffset b = c.place(twin[i].begin, true), e = b + width;
      t.eligible += width;
      const auto it = std::lower_bound(live.begin(), live.end(), b, byBegin);
      const bool there = it != live.end() && it->begin == b && it->end == e;
      // Leaves never overlap, so the last skipped one starting before `e` is
      // the only one that can reach past `b`.
      const auto sk = std::lower_bound(skipped.begin(), skipped.end(), e, byBegin);
      const bool swallowed = sk != skipped.begin() && std::prev(sk)->end > b;
      if (there && !abandoned(b, e))
        t.kept += width;
      else if (swallowed)
        t.deleted += width;
      else if (abandoned(b, e))
        t.abandoned += width;
      else
        t.other += width;
    }

    // The twin tokens the faults took, each with the stretch of faulted text
    // its surviving neighbours leave for it.
    struct Hole {
      std::size_t token;
      TextOffset lo, hi;
      bool taken;
    };
    std::vector<Hole> holes;
    for (const FaultWindow& w : windows)
      for (std::uint32_t i = w.coreFirst; i < w.coreLast && i < n; ++i) {
        const TextOffset lo =
            i == 0 ? 0 : c.place(twin[i - 1].end, true);
        const TextOffset hi = i + 1 < n
                                  ? c.place(twin[i + 1].begin, false)
                                  : static_cast<TextOffset>(c.text.size());
        holes.push_back(Hole{i, std::min(lo, w.at), std::max(hi, w.end), false});
      }
    const auto fill = [&](TextOffset b, TextOffset e, ElementId owed) {
      for (Hole& h : holes)
        if (!h.taken && h.lo <= b && e <= h.hi && restores(h.token, owed)) {
          h.taken = true;
          return true;
        }
      return false;
    };
    for (const CstLeaf& l : got) {
      if (!l.recovered || l.hidden) continue;
      const DiagnosticKind kind = l.begin == l.end ? DiagnosticKind::inserted
                                                   : DiagnosticKind::replaced;
      const ParseDiagnostic* said = nullptr;
      for (const ParseDiagnostic& d : diagnostics)
        if (d.kind == kind && d.begin <= l.begin && l.end <= d.end &&
            (kind == DiagnosticKind::replaced || d.begin == l.begin)) {
          said = &d;
          break;
        }
      if (said == nullptr) continue;
      if (fill(l.begin, l.end, said->element))
        ++t.restored;
      else
        ++t.invented;
    }
    return t;
  }
};

}  // namespace cursiv::recovery_metric
