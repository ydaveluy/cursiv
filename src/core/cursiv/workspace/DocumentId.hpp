#pragma once

#include <cstdint>
#include <limits>

// The identity of one managed document, as a number.

namespace cursiv {

// A document's identity, smaller than its URI: a description keeps it past its
// document. Same URI, same id, for ever.
enum class DocumentId : std::uint32_t {};
inline constexpr DocumentId kNoDocument{
    std::numeric_limits<std::uint32_t>::max()};

}  // namespace cursiv
