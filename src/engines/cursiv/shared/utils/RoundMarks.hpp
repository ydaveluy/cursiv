// Marks over ids that a new round empties whole: a round costs only what it
// marks.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cursiv::engine {

class RoundMarks {
 public:
  explicit RoundMarks(std::size_t size = 0) : _marks(size, 0u) {}

  void next() {
    if (++_round == 0) {
      std::ranges::fill(_marks, 0u);
      _round = 1;
    }
  }
  // Whether `id` is marked for the first time this round. It is marked after.
  bool first(std::size_t id) {
    if (id >= _marks.size())
      _marks.resize(std::max(id + 1, _marks.size() * 2), 0u);
    return std::exchange(_marks[id], _round) != _round;
  }

 private:
  std::vector<std::uint32_t> _marks;
  std::uint32_t _round = 1;
};

}  // namespace cursiv::engine
