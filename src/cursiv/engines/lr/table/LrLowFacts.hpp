// The lowering's facts a reading reads, beside its rows: no row lives here,
// so they copy whole.
#pragma once

#include <cstdint>

#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// Nothing a build concluded lives here: a frozen table would carry it at its
// default, a value and not an absence.
struct LrLowFacts {
  std::uint16_t nterms = 0;
  std::uint16_t nnts = 0;
  std::uint16_t nprods = 0;
  // The entry's own creation, which a wrap still owes: it ends the document
  // without reducing the entry. `kNoRow` where the entry creates nothing.
  std::uint16_t entryRow = kNoRow;
  // The class a keyword may not run into, as bytes; its scalars past ASCII
  // stand beside the sets.
  ByteSet wordBytes{};
};

}  // namespace cursiv
