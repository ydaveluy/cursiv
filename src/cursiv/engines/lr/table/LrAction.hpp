// One table entry of the LR automaton, on one terminal.
#pragma once

#include <cstdint>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/engines/lr/table/LrActionKind.hpp"

namespace cursiv {

struct LrAction {
  std::uint16_t term = 0;
  LrActionKind kind = LrActionKind::shift;
  std::uint16_t arg = 0;  // shift: target state ; reduce: production
  // The call site a completion caret expects here, `kNoElement` where the
  // sites the state serves disagree.
  ElementId site = kNoElement;
  // The enclosing rule's entry site, read where the position names none.
  ElementId outer = kNoElement;
  // The cell's order is the election's for every text.
  bool exact = false;

  constexpr bool operator==(const LrAction&) const = default;
};

// The op a table keeps of an action, as wide as its largest needs.
constexpr std::uint32_t lr_op_of(const LrAction& x) {
  return (std::uint32_t{x.arg} << 2) | static_cast<std::uint8_t>(x.kind);
}
constexpr LrAction lr_action_of(std::uint32_t op) {
  return LrAction{0, static_cast<LrActionKind>(op & 3u),
                  static_cast<std::uint16_t>(op >> 2)};
}

}  // namespace cursiv
