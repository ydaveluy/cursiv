#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

// A byte offset into the source, and the bound it implies: what the core
// measures with. The client's line/column unit is `Position`.

namespace cursiv {

using TextOffset = std::uint32_t;

// One byte short of the offsets' range: a document of line breaks has one
// line more than bytes, and a line number is 32 bits too.
inline constexpr std::size_t kMaxDocument =
    std::numeric_limits<TextOffset>::max() - 1;

}  // namespace cursiv
