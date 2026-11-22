#pragma once

#include "cursiv/utils/ConcurrentTable.hpp"

namespace support {

// Frees the released tables no reader can still hold: a table collects as it
// goes.
inline void collect_pending_tables() {
  const cursiv::detail::ConcurrentTable<int, int> gone;
}

}  // namespace support
