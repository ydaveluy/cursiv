// What the tables retired, collected as a program gets it collected: one
// retirement large enough to start a collection at once.
#pragma once

#include <cstdint>

#include "cursiv/utils/ConcurrentTable.hpp"

namespace support {

// Frees what no reader can still hold. The count of retirements starts over,
// so the few small ones a test makes next start no collection of their own.
inline void collect_retired_tables() {
  cursiv::detail::ConcurrentTable<int, std::uint64_t> large;
  for (int k = 0; k < 200000; ++k) {
    std::uint64_t value = 0;
    (void)large.insert(large.generation(), k, value);
  }
  large.clear();
}

}  // namespace support
