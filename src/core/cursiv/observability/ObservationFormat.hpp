#pragma once

#include <string>
#include <string_view>

#include "cursiv/observability/ObservabilitySink.hpp"

// The one-line rendering of an observation, exposed for the sink a host
// writes. Control characters are flattened to spaces, so one observation is
// one line.

namespace cursiv {

[[nodiscard]] std::string format_observation(const Observation&);
[[nodiscard]] std::string_view to_string(ObservationSeverity) noexcept;
[[nodiscard]] std::string_view to_string(ObservationCode) noexcept;

}  // namespace cursiv
