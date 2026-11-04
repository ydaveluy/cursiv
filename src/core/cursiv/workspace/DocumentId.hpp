#pragma once

#include <cstdint>
#include <limits>

// The identity of one managed document, as a number.

namespace cursiv {

// A document could be identified by its `uri` alone; this type exists
// because a description that outlives its document must stay nameable, and a
// URI string per description is a string per symbol. Same URI, same id, for
// ever.
enum class DocumentId : std::uint32_t {};
inline constexpr DocumentId kNoDocument{
    std::numeric_limits<std::uint32_t>::max()};

}  // namespace cursiv
