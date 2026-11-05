#pragma once

#include <atomic>

namespace cursiv::detail {

// One step of a busy wait: tells the core this thread is spinning.
void pause_once() noexcept;

// A lock for sections a few lookups long. Taking it costs one atomic exchange
// and releasing it a plain store, where a mutex pays an atomic operation for
// each. A thread that finds it taken waits twice as long at every look, then
// yields, so many threads on one lock do not fight over it. Not for a section
// that can block or run long.
class SpinLock {
 public:
  void lock() noexcept {
    if (!_taken.exchange(true, std::memory_order_acquire)) [[likely]]
      return;
    wait();
  }
  void unlock() noexcept { _taken.store(false, std::memory_order_release); }

 private:
  void wait() noexcept;

  std::atomic<bool> _taken{false};
};

}  // namespace cursiv::detail
