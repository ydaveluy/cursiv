// How a run of the parse states ends.
#pragma once

#include <cstdint>

namespace cursiv::engine {

// `pause` is not a failure: the state stopped on a lookahead it has no action
// for, which a repair search resolves.
enum class LrExit : std::uint8_t { accept, error, pause };

}  // namespace cursiv::engine
