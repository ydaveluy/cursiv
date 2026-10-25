#include "cursiv/core/observability/StderrObservabilitySink.hpp"

#include <cstdio>
#include <mutex>
#include <string>

#include "cursiv/core/observability/ObservationFormat.hpp"

namespace cursiv {

void StderrObservabilitySink::publish(const Observation& observation) noexcept {
  try {
    const std::string line = format_observation(observation);
    // One static mutex, not a member: the sink is replaceable and two
    // instances would otherwise interleave on the same stream.
    static std::mutex mutex;
    const std::scoped_lock lock(mutex);
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
  } catch (...) {
    // A report must never become a second failure.
  }
}

}  // namespace cursiv
