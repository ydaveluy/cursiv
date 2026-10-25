// What a reduce does with the records beneath it.
#pragma once

#include <cstdint>

namespace cursiv {

enum class LrReduceForm : std::uint8_t {
  through,    // retain the frame and change its state
  discard,    // no row is handed back or laid
  carry,      // hand back one record and its row
  close,      // lay this production's own record
  foldClose,  // fold onto one record if adjacent, otherwise lay a new one
};

}  // namespace cursiv
