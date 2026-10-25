// What the matcher hands back: one match, its end and its terminal -- and, of
// a candidate, the tier of the branch that read it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "cursiv/core/utils/ImplicitLifetime.hpp"
#include "cursiv/engines/shared/table/Token.hpp"

namespace cursiv {

struct Scan {
  TextOffset end = 0;
  SymbolId term = kInvalidSymbol;
  std::uint8_t tier = 0;
};

// Room for `N` matches the matcher fills before they are read: declaring it
// writes none of `Scan`'s defaults. It keeps no pointer to itself, which
// would be stored in the frame at every use.
template <std::size_t N>
class ScanRoom {
 public:
  [[nodiscard]] Scan* data() noexcept {
    return start_implicit_array<Scan>(_bytes, N);
  }
  [[nodiscard]] Scan& operator[](std::size_t i) noexcept { return data()[i]; }

 private:
  alignas(Scan) std::byte _bytes[sizeof(Scan) * N];
};

}  // namespace cursiv
