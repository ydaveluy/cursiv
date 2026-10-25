#include "cursiv/bench/LanguageBench.hpp"
#include "cursiv/test/Check.hpp"

using cursiv::bench::kAbsent;
using cursiv::bench::ServiceMeasurement;

int main() {
  ServiceMeasurement single;
  CHECK(single.samples == 0);
  CHECK(single.min == kAbsent);
  CHECK(single.ms == kAbsent);
  CHECK(single.worst == kAbsent);

  single.record(9.0);
  CHECK(single.samples == 1);
  CHECK(single.min == 9.0);
  CHECK(single.ms == 9.0);
  CHECK(single.worst == 9.0);

  // Include every repetition of every probe, even slower calls and zeroes.
  ServiceMeasurement sweep;
  for (const double elapsed : {3.0, 9.0, 6.0, 30.0, 12.0, 24.0})
    sweep.record(elapsed);
  CHECK(sweep.samples == 6);
  CHECK(sweep.min == 3.0);
  CHECK(sweep.ms == 14.0);
  CHECK(sweep.worst == 30.0);
  sweep.record(0.0);
  CHECK(sweep.samples == 7);
  CHECK(sweep.min == 0.0);
  CHECK(sweep.ms == 12.0);
  CHECK(sweep.worst == 30.0);

  return cursiv::test::report("ServiceMeasurement");
}
