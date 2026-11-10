// What the readings of open tokens may read where a run of bytes opens one
// from every position: a share of the text, and a floor for the short ones.
// Once spent, a reading gives nothing and the run is taken whole.
#pragma once

#include <cstdint>

#include "cursiv/text/TextOffset.hpp"

namespace cursiv::engine {

class LexReadBudget {
 public:
  explicit LexReadBudget(TextOffset len) : _left(kFloor + kShare * len) {}

  bool spent() const { return _left == 0; }

  void spend(TextOffset from, TextOffset reached) {
    const std::uint64_t bytes = reached - from;
    _left = bytes < _left ? _left - bytes : 0;
  }

 private:
  static constexpr std::uint64_t kShare = 4;
  static constexpr std::uint64_t kFloor = 1024;

  std::uint64_t _left;
};

}  // namespace cursiv::engine
