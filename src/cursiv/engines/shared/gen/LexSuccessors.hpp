#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

class LexSuccessors {
 public:
  explicit LexSuccessors(std::size_t count) : _seen(count, kDeadState) {
    base.reserve(count + 1u);
  }

  // First-class order determines which edges a bounded typo search retains.
  void append(std::span<const std::uint16_t> row) {
    const auto state = static_cast<std::uint16_t>(base.size() - 1u);
    for (const auto to : row) {
      if (to == kDeadState || _seen[to] == state) continue;
      _seen[to] = state;
      states.push_back(to);
    }
    base.push_back(static_cast<std::uint32_t>(states.size()));
  }

  std::vector<std::uint32_t> base{0};
  std::vector<std::uint16_t> states;

 private:
  std::vector<std::uint16_t> _seen;
};

}  // namespace cursiv
