// What the predictions of one parse read: tokens, and the readings they stepped
// over them.
#pragma once

#include <cstdint>

namespace cursiv {

struct LlPredictionWork {
  std::uint64_t tokens = 0;
  std::uint64_t readings = 0;
};

}  // namespace cursiv
