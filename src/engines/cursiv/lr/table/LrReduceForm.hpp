// What a reduce does with the records beneath it.
#pragma once

#include <cstdint>

namespace cursiv::engine {

enum class LrReduceForm : std::uint8_t {
  through,    // retain the frame and change its state
  discard,    // no row is handed back or written
  carry,      // hand back one record and its row
  close,      // create this production's own record
  foldClose,  // fold onto one record if adjacent, otherwise create a new one
};

}  // namespace cursiv::engine
