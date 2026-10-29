// The operators of a table's prefix and postfix levels, against the rest of
// it. The same text on two prefix levels, or on two postfix levels, reads one
// operand two ways; a postfix operator that is also binary leaves the text
// after an operand two readings that no token tells apart. An operator whose
// strings cannot be listed may clash, and is refused where it could.
#pragma once

#include <cstdint>
#include <span>

#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"

namespace cursiv {

// The first refusal the tables of `rules` earn, a rule named by its position
// in `rules` plus one; none where every table is sound.
GrammarFault unary_level_fault(GrammarImage& image,
                               std::span<const std::uint32_t> rules);

}  // namespace cursiv
