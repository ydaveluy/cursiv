#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

// The distinct successors of each of `count` DFA states, whose rows of
// `ncls` cells `next` holds: first-class order determines which edges a
// bounded typo search retains.
struct LexSuccessors {
  LexSuccessors(const std::uint16_t* next, std::size_t count, unsigned ncls) {
    std::vector<std::uint16_t> seen(count, kDeadState);
    base.reserve(count + 1u);
    for (std::size_t s = 0; s < count; ++s) {
      const auto state = static_cast<std::uint16_t>(s);
      for (const auto to : std::span{next + s * ncls, ncls}) {
        if (to == kDeadState || seen[to] == state) continue;
        seen[to] = state;
        states.push_back(to);
      }
      base.push_back(static_cast<std::uint32_t>(states.size()));
    }
  }

  std::vector<std::uint32_t> base{0};
  std::vector<std::uint16_t> states;
};

}  // namespace cursiv
