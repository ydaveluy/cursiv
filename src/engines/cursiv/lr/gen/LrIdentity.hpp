// Which states the construction keeps apart: the wider graph is built only
// where the core's merge may have made an ambiguity.
#pragma once

#include <cstdint>

namespace cursiv::engine {

enum class LrIdentity : std::uint8_t { core_plus_lookaheads, core };

}  // namespace cursiv::engine
