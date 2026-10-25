// A policy over the walk, never a second writer. A guide may say which
// alternative to take; it cannot emit a token, a span or a member. That is
// the whole of what the LR graph is allowed to do here -- steer, and be
// checked.
#pragma once

#include <cstddef>
#include <span>

#include "cursiv/core/grammar/ElementId.hpp"

namespace cursiv::fuzzer {

struct GrammarGuide {
  virtual ~GrammarGuide() = default;
  // The alternative to take, or -1 to leave the choice to the ribbon.
  // `emitted` is how many tokens the walk has written so far.
  [[nodiscard]] virtual int prefer(std::span<const ElementId> kids,
                                   std::size_t emitted) const = 0;
  // One more round of a repetition: 1 takes it, 0 stops at the declared
  // minimum, -1 leaves the count to the ribbon. `kid` is what the next round
  // would begin with, which is the separator where there is one.
  [[nodiscard]] virtual int another(ElementId kid,
                                    std::size_t emitted) const = 0;
};

}  // namespace cursiv::fuzzer
