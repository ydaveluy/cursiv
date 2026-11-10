#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cursiv/utils/AtomicWait.hpp"

// From Taskflow (taskflow/core/atomic_notifier.hpp), MIT licence:
//   Copyright (c) 2018-2024 Dr. Tsung-Wei Huang, University of Wisconsin.

// Parks a worker without losing a wake-up: one 64-bit word holds an epoch
// above a waiter count, and a notify moves the epoch. A worker that misses
// 2^32 notifications before it sleeps waits for the next one.

namespace cursiv::detail {

class AtomicNotifier {
 public:
  // One per worker, kept on its own line: the epoch it last saw.
  struct alignas(128) Waiter {
    std::uint32_t epoch = 0;
  };

  explicit AtomicNotifier(std::size_t waiters) : _waiters(waiters) {}
  AtomicNotifier(const AtomicNotifier&) = delete;
  AtomicNotifier& operator=(const AtomicNotifier&) = delete;

  [[nodiscard]] Waiter& waiter(std::size_t index) noexcept {
    return _waiters[index];
  }

  // Wakes as many as there is work for, or everyone past the waiter count. One
  // fence orders the whole batch.
  void notifyN(std::size_t count) noexcept {
    if (count == 0) return;
    if (count >= _waiters.size()) {
      notifyAll();
      return;
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (std::size_t woken = 0; woken < count; ++woken)
      if (!wakeOne()) return;  // nobody left asleep
  }

  void notifyAll() noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (std::uint64_t state = _state.load(std::memory_order_acquire);
         (state & kWaiterMask) != 0;)
      if (_state.compare_exchange_weak(state, state + kEpochInc,
                                       std::memory_order_acquire)) {
        detail::wake_all(_state);
        break;
      }
  }

  // Register, check for work again, then commit: without the second check,
  // work published between the last search and the sleep wakes nobody.
  void prepareWait(Waiter& waiter) noexcept {
    const std::uint64_t prev =
        _state.fetch_add(kWaiterInc, std::memory_order_relaxed);
    waiter.epoch = static_cast<std::uint32_t>(prev >> kEpochShift);
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  void cancelWait() noexcept {
    _state.fetch_sub(kWaiterInc, std::memory_order_seq_cst);
  }

  void commitWait(const Waiter& waiter) noexcept {
    std::uint64_t prev = _state.load(std::memory_order_acquire);
    while (static_cast<std::uint32_t>(prev >> kEpochShift) == waiter.epoch) {
      detail::wait_while_equal(_state, prev, std::memory_order_acquire);
      prev = _state.load(std::memory_order_acquire);
    }
    // Relaxed would be correct; seq_cst brings the waiter count to zero sooner,
    // so fewer spurious wake-ups, each a system call.
    _state.fetch_sub(kWaiterInc, std::memory_order_seq_cst);
  }

 private:
  // Moves the epoch and wakes one sleeper. Returns false when no worker waits,
  // the common case on a busy pool.
  bool wakeOne() noexcept {
    for (std::uint64_t state = _state.load(std::memory_order_acquire);
         (state & kWaiterMask) != 0;)
      if (_state.compare_exchange_weak(state, state + kEpochInc,
                                       std::memory_order_acquire)) {
        detail::wake_one(_state);
        return true;
      }
    return false;
  }

  // The epoch in the high 32 bits, the waiter count in the low 32.
  std::atomic<std::uint64_t> _state{0};
  std::vector<Waiter> _waiters;

  static constexpr std::uint64_t kWaiterInc = 1;
  static constexpr std::uint64_t kEpochShift = 32;
  static constexpr std::uint64_t kEpochInc = std::uint64_t{1} << kEpochShift;
  static constexpr std::uint64_t kWaiterMask = kEpochInc - 1;
};

}  // namespace cursiv::detail
