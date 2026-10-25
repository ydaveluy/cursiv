#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// Taken from Taskflow (taskflow/core/atomic_notifier.hpp), MIT licence:
//   Copyright (c) 2018-2024 Dr. Tsung-Wei Huang, University of Wisconsin.
// The algorithm is unchanged; only the spelling follows this tree.

// Parks a worker without losing a wake-up. One 64-bit word packs an epoch
// above and a waiter count below; a notify moves the epoch, so a worker
// already announced never sleeps past a wake-up it missed the timing of.

namespace cursiv {

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

  // Waking exactly as many as there is work for; past the waiter count it is
  // cheaper to wake everyone than to walk the list.
  //
  // One fence for the whole batch: a run wakes one worker per part, and paying
  // a sequential-consistency fence per wake put dozens of them on the path that
  // publishes work -- while one before the batch orders it just as well.
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
        _state.notify_all();
        break;
      }
  }

  // Announce, then look again, then commit: skipping the second look is the
  // lost wake-up, since work published between the last search and the sleep
  // would wake nobody.
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
      _state.wait(prev, std::memory_order_acquire);
      prev = _state.load(std::memory_order_acquire);
    }
    // Seq_cst rather than relaxed: correctness would allow the looser order,
    // but the sooner the waiter count reaches zero the fewer spurious wake-ups
    // -- and each of those is a system call.
    _state.fetch_sub(kWaiterInc, std::memory_order_seq_cst);
  }

 private:
  // Moves the epoch and wakes one sleeper. Returns false when none was
  // announced, which is the common case on a pool that never went idle.
  bool wakeOne() noexcept {
    for (std::uint64_t state = _state.load(std::memory_order_acquire);
         (state & kWaiterMask) != 0;)
      if (_state.compare_exchange_weak(state, state + kEpochInc,
                                       std::memory_order_acquire)) {
        _state.notify_one();
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

}  // namespace cursiv
