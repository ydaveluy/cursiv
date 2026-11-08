#pragma once

#include <string>

#include "cursiv/observability/ObservabilitySink.hpp"

// The one-line rendering of an observation, the one the standard error sink
// writes. Control characters are flattened to spaces, so one observation is
// one line.

namespace cursiv::detail {

[[nodiscard]] std::string format_observation(const Observation&);

}  // namespace cursiv::detail
