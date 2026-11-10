// What the process holds at a phase boundary: two readings subtract into what
// a phase made resident. Compare one language across binaries, never two
// languages of one run.
#pragma once

#include <cstddef>
#include <cstdio>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace cursiv::bench::detail {

// Resident bytes now, and the highest this process has held. Each figure is
// zero and invalid where the platform does not answer it; the other columns
// stay.
struct RssSample {
  // Signed, so the stages sum to the total: a phase may give back more than it
  // took.
  std::ptrdiff_t current = 0;
  std::size_t peak = 0;
  bool valid = false;
  bool peakValid = false;
};

// Resets the process's high-water mark by writing `5` to `clear_refs`, once
// per build. When refused, the peak is the whole process's.
inline bool rss_reset() noexcept {
#ifdef __linux__
  std::FILE* const f = std::fopen("/proc/self/clear_refs", "w");
  if (f == nullptr) return false;
  const bool ok = std::fputs("5\n", f) >= 0;
  return std::fclose(f) == 0 && ok;
#else
  return false;
#endif
}

// `statm` for the current level and `getrusage` for the peak: both are cheap
// to read.
[[nodiscard]] inline RssSample rss_now() noexcept {
  RssSample s;
#ifdef __linux__
  std::FILE* const f = std::fopen("/proc/self/statm", "r");
  if (f == nullptr) return s;
  unsigned long size = 0;
  unsigned long resident = 0;
  const int got = std::fscanf(f, "%lu %lu", &size, &resident);
  std::fclose(f);
  if (got != 2) return s;
  const long page = ::sysconf(_SC_PAGESIZE);
  if (page <= 0) return s;
  s.current =
      static_cast<std::ptrdiff_t>(resident) * static_cast<std::ptrdiff_t>(page);
  if (rusage r{}; ::getrusage(RUSAGE_SELF, &r) == 0) {
    s.peak = static_cast<std::size_t>(r.ru_maxrss) * 1024u;
    s.peakValid = true;
  }
  s.valid = true;
#endif
  return s;
}

// What the phase made resident. The peak is the later of the two -- a mark
// only ever rises.
[[nodiscard]] inline RssSample rss_delta(const RssSample& from,
                                         const RssSample& to) noexcept {
  RssSample s;
  if (!from.valid || !to.valid) return s;
  s.current = to.current - from.current;
  s.peak = to.peak;
  s.valid = true;
  s.peakValid = to.peakValid;
  return s;
}

}  // namespace cursiv::bench::detail
