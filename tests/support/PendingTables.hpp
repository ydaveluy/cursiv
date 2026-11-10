#pragma once

#include <cstdint>

#include "cursiv/utils/ConcurrentTable.hpp"

namespace support {

// Frees the released tables no reader can still hold, through one release
// large enough to start a collection. The count starts over, so a test's next
// few small releases start none.
inline void collect_pending_tables() {
  cursiv::detail::ConcurrentTable<int, std::uint64_t> large;
  for (int k = 0; k < 200000; ++k) {
    std::uint64_t value = 0;
    (void)large.insert(large.generation(), k, value);
  }
  large.clear();
}

}  // namespace support
