#pragma once

#include "cursiv/core/observability/ObservabilitySink.hpp"

// One formatted line on stderr, stdout being the protocol's. Serialised so two
// threads' observations interleave line by line, never byte by byte.

namespace cursiv {

class StderrObservabilitySink final : public ObservabilitySink {
 public:
  void publish(const Observation&) noexcept override;
};

}  // namespace cursiv
