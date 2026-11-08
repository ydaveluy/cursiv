// What the predictions of one parse read, in tokens.
#pragma once

#include <cstdint>

namespace cursiv::engine {

struct LlPredictionWork {
  std::uint64_t tokens = 0;
  // The plan entries the descent read to be answered.
  std::uint64_t planVisits = 0;

  // What the parses of a thread have read, added as each one ends.
  static LlPredictionWork& current() {
    static thread_local LlPredictionWork spent;
    return spent;
  }
};

}  // namespace cursiv::engine
