// Where one fault may be answered, read on the twin and never on a repaired
// tree: the stretch the fault touches -- its bytes, the tokens and the trivia
// they reach -- widened by one token each side, then placed in the faulted
// text. A token flush against the fault is touched when the faulted text puts
// no blank between them: the lexer may glue the two.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "recovery_metric/support/RepairCase.hpp"
#include "recovery_metric/support/RepairFault.hpp"

namespace cursiv {

struct FaultWindow {
  // Twin tokens, half-open: the ones the fault touches.
  std::uint32_t coreFirst = 0, coreLast = 0;
  // Twin text, half-open: what the fault touches.
  TextOffset from = 0, to = 0;
  // Faulted text, closed: the window, and the bytes the fault wrote.
  TextOffset lo = 0, hi = 0;
  TextOffset at = 0, end = 0;

  [[nodiscard]] static std::vector<FaultWindow> of(
      std::span<const CstLeaf> tokens, std::span<const CstLeaf> hidden,
      const RepairCase& c) {
    std::vector<FaultWindow> out;
    out.reserve(c.faults.size());
    const auto solid = [&](std::size_t i) {
      return i < c.text.size() && static_cast<unsigned char>(c.text[i]) > ' ';
    };
    const auto n = static_cast<std::uint32_t>(tokens.size());
    for (std::size_t k = 0; k < c.faults.size(); ++k) {
      const RepairFault& f = c.faults[k];
      const TextOffset stop = f.at + f.removed;
      const auto reaches = [&](const CstLeaf& l) {
        return f.removed != 0 ? l.begin < stop && f.at < l.end
                              : l.begin < f.at && f.at < l.end;
      };
      FaultWindow w;
      w.at = c.placed(k);
      w.end = w.at + static_cast<TextOffset>(f.inserted.size());
      std::uint32_t i = 0;
      while (i < n && tokens[i].end <= f.at) ++i;
      std::uint32_t j = i;
      while (j < n && reaches(tokens[j])) ++j;
      if (i > 0 && tokens[i - 1].end == f.at && solid(w.at)) --i;
      if (j < n && tokens[j].begin == stop && w.end > 0 && solid(w.end - 1))
        ++j;
      w.coreFirst = i;
      w.coreLast = j;
      w.from = f.at;
      w.to = stop;
      if (i < j) {
        w.from = std::min(w.from, tokens[i].begin);
        w.to = std::max(w.to, tokens[j - 1].end);
      }
      for (const CstLeaf& h : hidden)
        if (reaches(h)) {
          w.from = std::min(w.from, h.begin);
          w.to = std::max(w.to, h.end);
        }
      std::uint32_t prev = 0;
      while (prev < n && tokens[prev].end <= w.from) ++prev;
      std::uint32_t next = prev;
      while (next < n && tokens[next].begin < w.to) ++next;
      w.lo = prev == 0 ? 0 : c.place(tokens[prev - 1].begin, true);
      w.hi = next >= n ? static_cast<TextOffset>(c.text.size())
                       : c.place(tokens[next].end, false);
      w.lo = std::min(w.lo, w.at);
      w.hi = std::max(w.hi, w.end);
      out.push_back(w);
    }
    return out;
  }
};

}  // namespace cursiv
