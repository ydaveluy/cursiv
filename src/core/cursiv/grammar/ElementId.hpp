#pragma once

#include <cstdint>

// The id of a grammar element, shared by a CST record and its grammar.

namespace cursiv {

// One flat id, in runs laid end to end: the grammar's own rows come first,
// so an id below the row count is that row, and a run above it resolves by
// subtraction to the row it stands for.
using ElementId = std::uint16_t;
// "Names no element". The element table is bounded to fit below it.
inline constexpr ElementId kNoElement = 0xFFFFu;

}  // namespace cursiv
