// A faulted document and the faults that made it from its valid twin. The
// faults are sorted and disjoint, so every twin offset no fault removes has
// exactly one place in the faulted text.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/text/TextOffset.hpp"
#include "recovery_metric/support/RepairFault.hpp"

namespace cursiv {

struct RepairCase {
  std::string text;
  std::vector<RepairFault> faults;

  // Refused when two faults overlap or touch, or one reaches past the text:
  // the offset map below would lie.
  [[nodiscard]] static bool make(std::string_view valid,
                                 std::vector<RepairFault> faults,
                                 RepairCase& out) {
    TextOffset from = 0;
    for (std::size_t i = 0; i < faults.size(); ++i) {
      const RepairFault& f = faults[i];
      if (f.at < from || (i != 0 && f.at == from) ||
          std::size_t{f.at} + f.removed > valid.size())
        return false;
      from = f.at + f.removed;
    }
    out.text.clear();
    TextOffset at = 0;
    for (const RepairFault& f : faults) {
      out.text.append(valid.substr(at, f.at - at));
      out.text += f.inserted;
      at = f.at + f.removed;
    }
    out.text.append(valid.substr(at));
    out.faults = std::move(faults);
    return true;
  }

  // Where a twin offset lands. Inside a removed range, or at an insertion
  // point, `before` answers the fault's first faulted byte and `!before` the
  // byte past what it wrote.
  [[nodiscard]] TextOffset place(TextOffset offset, bool before) const {
    long long delta = 0;
    for (const RepairFault& f : faults) {
      if (offset < f.at) break;
      const bool inside = offset < f.at + f.removed ||
                          (f.removed == 0 && offset == f.at);
      if (inside)
        return static_cast<TextOffset>(
            f.at + delta + (before ? 0 : static_cast<long long>(f.inserted.size())));
      delta += static_cast<long long>(f.inserted.size()) - f.removed;
    }
    return static_cast<TextOffset>(offset + delta);
  }

  // The first faulted byte fault `i` wrote.
  [[nodiscard]] TextOffset placed(std::size_t i) const {
    long long delta = 0;
    for (std::size_t j = 0; j < i; ++j)
      delta += static_cast<long long>(faults[j].inserted.size()) -
               faults[j].removed;
    return static_cast<TextOffset>(faults[i].at + delta);
  }
};

}  // namespace cursiv
