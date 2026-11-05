#include "cursiv/execution/SpinLock.hpp"

#include <algorithm>
#include <thread>

namespace cursiv::detail {

void pause_once() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#else
  std::this_thread::yield();
#endif
}

void SpinLock::wait() noexcept {
  constexpr unsigned kMaxPauses = 1024;
  for (unsigned pauses = 1;; pauses = std::min(pauses * 2, kMaxPauses)) {
    for (unsigned i = 0; i < pauses; ++i) pause_once();
    if (pauses == kMaxPauses) std::this_thread::yield();
    if (!_taken.load(std::memory_order_relaxed) &&
        !_taken.exchange(true, std::memory_order_acquire))
      return;
  }
}

}  // namespace cursiv::detail
