#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

// A byte offset into the source, and the bound it implies: what the core
// measures with. The client's line/column unit is `Position`, one storey up.

namespace cursiv {

using TextOffset = std::uint32_t;

inline constexpr std::size_t kMaxDocument =
    std::numeric_limits<TextOffset>::max();

}  // namespace cursiv
