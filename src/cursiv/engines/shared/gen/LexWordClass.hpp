// A section's word class: the bytes and scalars a keyword of it may not run
// into, and the set that bounds it.
#pragma once

#include <cstdint>
#include <vector>

#include "cursiv/core/grammar/unicode/UnicodeRange.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/TermInfo.hpp"

namespace cursiv {

struct LexWordClass {
  ByteSet bytes;
  std::vector<unicode::Range> wide;
  std::uint16_t set = kNoSet;
};

}  // namespace cursiv
