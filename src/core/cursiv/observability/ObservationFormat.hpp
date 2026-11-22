#pragma once

#include <string>

#include "cursiv/observability/ObservabilitySink.hpp"

// The one-line rendering of an observation: the one the standard error sink
// writes, and one a sink of your own can write. Control characters are
// flattened to spaces, so one observation is one line.

namespace cursiv {

[[nodiscard]] std::string formatObservation(const Observation&);

}  // namespace cursiv
