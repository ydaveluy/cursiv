// Hardware counters for the calling thread, optional when the PMU is denied.
// A group per CPU PMU follows the thread across Intel hybrid P/E cores without
// changing its affinity. Events within each group share a scheduling window.
#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace cursiv::bench {

struct PerfReading {
  std::uint64_t cycles = 0;
  std::uint64_t instructions = 0;
  std::uint64_t branchMisses = 0;
  std::uint64_t cacheMisses = 0;
  std::uint64_t enabled = 0;
  std::uint64_t running = 0;
  bool valid = false;
};

// Cumulative reads retain each PMU's times: a dormant PMU must not cause the
// active PMU's counts to be extrapolated over the same region a second time.
struct PerfSample : PerfReading {
  bool scaled = false;
  std::array<PerfReading, 2> pmu{};
  std::size_t pmuCount = 0;
};

class PerfCounters {
 public:
  PerfCounters() noexcept { open(); }
  ~PerfCounters() { close(); }
  PerfCounters(const PerfCounters&) = delete;
  PerfCounters& operator=(const PerfCounters&) = delete;

  [[nodiscard]] bool active() const noexcept { return _groupCount != 0; }

  void reset() const noexcept {
#ifdef __linux__
    control(PERF_EVENT_IOC_RESET);
#endif
  }
  void enable() const noexcept {
#ifdef __linux__
    control(PERF_EVENT_IOC_ENABLE);
#endif
  }
  void disable() const noexcept {
#ifdef __linux__
    control(PERF_EVENT_IOC_DISABLE);
#endif
  }

  [[nodiscard]] PerfSample read() const noexcept {
    PerfSample s;
#ifdef __linux__
    if (!active()) return s;
    s.pmuCount = _groupCount;
    for (std::size_t i = 0; i < _groupCount; ++i) {
      std::uint64_t buf[3 + kCount] = {};  // nr, enabled, running, values
      ssize_t n;
      do {
        n = ::read(_groups[i].fd[0], buf, sizeof buf);
      } while (n < 0 && errno == EINTR);
      if (n != static_cast<ssize_t>(sizeof buf) || buf[0] != kCount) return s;
      PerfReading& r = s.pmu[i];
      r.enabled = buf[1];
      r.running = buf[2];
      r.cycles = buf[3];
      r.instructions = buf[4];
      r.branchMisses = buf[5];
      r.cacheMisses = buf[6];
      r.valid = true;
      s.cycles += r.cycles;
      s.instructions += r.instructions;
      s.branchMisses += r.branchMisses;
      s.cacheMisses += r.cacheMisses;
      s.enabled = r.enabled > s.enabled ? r.enabled : s.enabled;
      s.running += r.running;
    }
    s.valid = true;
#endif
    return s;
  }

 private:
  static constexpr std::size_t kCount = 4;
  struct Group {
    int fd[kCount] = {-1, -1, -1, -1};
  };

#ifdef __linux__
  static bool pmu_type(const char* path, std::uint32_t& type) noexcept {
    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) return false;
    unsigned value = 0;
    const bool found = std::fscanf(file, "%u", &value) == 1 && value != 0;
    std::fclose(file);
    type = value;
    return found;
  }

  bool open_group(std::uint32_t pmuType) noexcept {
    static constexpr std::uint64_t kEvent[kCount] = {
        PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS,
        PERF_COUNT_HW_BRANCH_MISSES, PERF_COUNT_HW_CACHE_MISSES};
    Group& group = _groups[_groupCount];
    for (std::size_t i = 0; i < kCount; ++i) {
      perf_event_attr attr{};
      attr.size = sizeof attr;
      attr.type = PERF_TYPE_HARDWARE;
      // Extended hardware events select a hybrid PMU in config[63:32].
      attr.config = (static_cast<std::uint64_t>(pmuType) << 32) | kEvent[i];
      attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED |
                         PERF_FORMAT_TOTAL_TIME_RUNNING;
      attr.disabled = i == 0 ? 1u : 0u;
      attr.exclude_kernel = 1;
      attr.exclude_hv = 1;
      attr.inherit = 0;
      const long fd =
          ::syscall(__NR_perf_event_open, &attr, 0, -1,
                    i == 0 ? -1 : group.fd[0], PERF_FLAG_FD_CLOEXEC);
      if (fd < 0) return false;
      group.fd[i] = static_cast<int>(fd);
    }
    ++_groupCount;
    return true;
  }
#endif

  void open() noexcept {
#ifdef __linux__
    std::uint32_t core = 0, atom = 0;
    const bool hybrid =
        pmu_type("/sys/bus/event_source/devices/cpu_core/type", core) &&
        pmu_type("/sys/bus/event_source/devices/cpu_atom/type", atom);
    if (hybrid) {
      if (!open_group(core) || !open_group(atom)) close();
    } else if (!open_group(0)) {
      close();
    }
#endif
  }

  void close() noexcept {
#ifdef __linux__
    for (Group& group : _groups)
      for (std::size_t i = kCount; i-- > 0;)
        if (group.fd[i] >= 0) {
          ::close(group.fd[i]);
          group.fd[i] = -1;
        }
#endif
    _groupCount = 0;
  }

#ifdef __linux__
  void control(unsigned long request) const noexcept {
    for (std::size_t i = 0; i < _groupCount; ++i)
      ::ioctl(_groups[i].fd[0], request, PERF_IOC_FLAG_GROUP);
  }
#endif

  std::array<Group, 2> _groups{};
  std::size_t _groupCount = 0;
};

inline PerfSample perf_delta(const PerfSample& from, const PerfSample& to) {
  PerfSample d;
  if (!from.valid || !to.valid || from.pmuCount != to.pmuCount) return d;
  std::uint64_t dominantRunning = 0;
  const auto accumulate = [&](const PerfReading& a, const PerfReading& b) {
    if (!a.valid || !b.valid || b.enabled < a.enabled ||
        b.running < a.running || b.cycles < a.cycles ||
        b.instructions < a.instructions || b.branchMisses < a.branchMisses ||
        b.cacheMisses < a.cacheMisses)
      return false;
    const std::uint64_t running = b.running - a.running;
    if (running == 0) return true;  // the thread stayed on the other CPU type
    d.cycles += b.cycles - a.cycles;
    d.instructions += b.instructions - a.instructions;
    d.branchMisses += b.branchMisses - a.branchMisses;
    d.cacheMisses += b.cacheMisses - a.cacheMisses;
    d.running += running;
    // Every group is enabled for the same thread interval. Use the one with
    // most coverage, avoiding the read skew of a PMU that never ran.
    if (running > dominantRunning) {
      dominantRunning = running;
      d.enabled = b.enabled - a.enabled;
    }
    return true;
  };
  if (from.pmuCount == 0) {
    if (!accumulate(from, to)) return {};
  } else {
    for (std::size_t i = 0; i < from.pmuCount; ++i)
      if (!accumulate(from.pmu[i], to.pmu[i])) return {};
  }
  if (d.running == 0) return d;
  // Sum actual counts across CPU types before correcting for PMU contention.
  // Scaling each PMU to the full interval would double-count migrations.
  if (d.running < d.enabled) {
    const double lift =
        static_cast<double>(d.enabled) / static_cast<double>(d.running);
    d.cycles = static_cast<std::uint64_t>(static_cast<double>(d.cycles) * lift);
    d.instructions =
        static_cast<std::uint64_t>(static_cast<double>(d.instructions) * lift);
    d.branchMisses =
        static_cast<std::uint64_t>(static_cast<double>(d.branchMisses) * lift);
    d.cacheMisses =
        static_cast<std::uint64_t>(static_cast<double>(d.cacheMisses) * lift);
    d.scaled = true;
  }
  d.valid = true;
  return d;
}

}  // namespace cursiv::bench
