#pragma once

#include <thread>

namespace cursiv::detail {

// One step of a busy wait: tells the core this thread is spinning.
inline void pause_once() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#else
  std::this_thread::yield();
#endif
}

}  // namespace cursiv::detail
