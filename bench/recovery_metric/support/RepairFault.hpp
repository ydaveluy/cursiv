// One fault planted in a valid document, in the valid text's offsets: the
// bytes `[at, at + removed)` give way to `inserted`.
#pragma once

#include <string>

#include "cursiv/core/text/TextOffset.hpp"

namespace cursiv {

struct RepairFault {
  TextOffset at = 0;
  TextOffset removed = 0;
  std::string inserted;
};

}  // namespace cursiv
