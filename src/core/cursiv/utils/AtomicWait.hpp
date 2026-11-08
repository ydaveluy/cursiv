#pragma once

#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#if defined(__wasm__) && defined(__wasm_atomics__)
#include <chrono>
#include <thread>
#endif

// `std::atomic::wait` and its notifications, made to block on every target.
// On WASI, libc++ has no futex to call: its wait polls the value and sleeps
// between polls, for up to 8 ms at a time, and its notify wakes nobody. A
// parked thread then starts the work it was woken for that much later. A
// shared wasm memory has its own futex, `memory.atomic.wait` and `notify`, and
// these use it. Elsewhere they are the standard calls.

namespace cursiv::detail {

#if defined(__wasm__) && defined(__wasm_atomics__)
// The window libc++ polls in before it sleeps.
inline constexpr std::chrono::microseconds kPollBeforeSleep{64};
#endif

// Returns once `word`, read with `order`, no longer holds `old`.
template <class T>
void wait_while_equal(const std::atomic<T>& word, T old,
                      std::memory_order order) noexcept {
#if defined(__wasm__) && defined(__wasm_atomics__)
  static_assert(sizeof(std::atomic<T>) == sizeof(T) &&
                    (sizeof(T) == 4 || sizeof(T) == 8),
                "a wasm futex waits on a 32- or 64-bit word");
  // A futex wake takes tens of microseconds under a wasm engine, longer than
  // many waits last. So the first microseconds are polled, giving the core
  // away between reads, as libc++ polls before it sleeps.
  const auto pollUntil = std::chrono::steady_clock::now() + kPollBeforeSleep;
  // The futex reads the word itself, so it needs a mutable address; it never
  // writes through it.
  auto* const address = const_cast<std::atomic<T>*>(&word);
  while (word.load(order) == old) {
    if (std::chrono::steady_clock::now() < pollUntil) {
      std::this_thread::yield();
      continue;
    }
    // An infinite timeout: the loop, not a clock, decides when to stop.
    if constexpr (sizeof(T) == 4)
      __builtin_wasm_memory_atomic_wait32(
          reinterpret_cast<std::int32_t*>(address),
          std::bit_cast<std::int32_t>(old), -1);
    else
      __builtin_wasm_memory_atomic_wait64(
          reinterpret_cast<std::int64_t*>(address),
          std::bit_cast<std::int64_t>(old), -1);
  }
#else
  word.wait(old, order);
#endif
}

template <class T>
void wake_one(std::atomic<T>& word) noexcept {
#if defined(__wasm__) && defined(__wasm_atomics__)
  __builtin_wasm_memory_atomic_notify(reinterpret_cast<std::int32_t*>(&word),
                                      1);
#else
  word.notify_one();
#endif
}

template <class T>
void wake_all(std::atomic<T>& word) noexcept {
#if defined(__wasm__) && defined(__wasm_atomics__)
  __builtin_wasm_memory_atomic_notify(reinterpret_cast<std::int32_t*>(&word),
                                      std::numeric_limits<std::uint32_t>::max());
#else
  word.notify_all();
#endif
}

}  // namespace cursiv::detail
