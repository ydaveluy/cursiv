#pragma once

#include <string>
#include <string_view>

#include "cursiv/core/observability/ObservabilitySink.hpp"

// The one-line rendering of an observation, exposed for the sink a host
// writes. Newlines are flattened, so one observation is one line.

namespace cursiv {

[[nodiscard]] std::string format_observation(const Observation&);
[[nodiscard]] std::string_view to_string(ObservationSeverity) noexcept;
[[nodiscard]] std::string_view to_string(ObservationCode) noexcept;

}  // namespace cursiv
