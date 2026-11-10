// A policy over the walk, never a second writer: a guide may choose an
// alternative, but never emit a token, a span or a member.
#pragma once

#include <cstddef>
#include <span>

#include "cursiv/grammar/ElementId.hpp"

namespace cursiv::fuzzer::detail {

struct GrammarGuide {
  virtual ~GrammarGuide() = default;
  // The alternative to take, or -1 to leave the choice to the source.
  // `emitted` is how many tokens the walk has written so far.
  [[nodiscard]] virtual int prefer(std::span<const ElementId> children,
                                   std::size_t emitted) const = 0;
  // One more round of a repetition: 1 takes it, 0 stops at the declared
  // minimum, -1 leaves the count to the source. `child` is what the next round
  // would begin with, which is the separator where there is one.
  [[nodiscard]] virtual int another(ElementId child,
                                    std::size_t emitted) const = 0;
};

}  // namespace cursiv::fuzzer::detail
