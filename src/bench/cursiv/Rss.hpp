// What the process holds, read at a phase boundary: a level, not a counter,
// so two of these subtract into what a phase made resident. Comparable
// across binaries (one language per process); not comparable between
// languages within one run -- the allocator there is already warm.
#pragma once

#include <cstddef>
#include <cstdio>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace cursiv::bench {

// Resident bytes now, and the highest this process has held. Zero and invalid
// where the platform does not answer -- the ladder keeps its other columns.
struct RssSample {
  // Signed, so the stages sum to the total: a phase that gives more back
  // than it took has a figure of its own, and clamping it to zero would
  // make the ladder add up to something it never was.
  std::ptrdiff_t current = 0;
  std::size_t peak = 0;
  bool valid = false;
};

// Writing `5` to `clear_refs` resets the process high-water mark, the only
// way a peak means this build's and not a previous language's -- spent once,
// outside the timed window, never per phase. Where the write is refused, the
// peak column silently reads as the whole process's.
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

// `statm` for the level -- one line of seven numbers, where `status` would be
// fifty to parse -- and `getrusage` for the mark, which costs no parsing at
// all.
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
  if (rusage r{}; ::getrusage(RUSAGE_SELF, &r) == 0)
    s.peak = static_cast<std::size_t>(r.ru_maxrss) * 1024u;
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
  return s;
}

}  // namespace cursiv::bench
