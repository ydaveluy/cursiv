#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "cursiv/utils/ConcurrentTable.hpp"

namespace cursiv::detail {

// A workspace question with no key dimension. Values are immutable once
// published. Like ConcurrentTable, a read by reference holds the epoch until
// its consumer returns, even when a concurrent clear releases the slot.
template <class V>
class ConcurrentValue {
  // A slot is empty, then ready once a value is published. A clear with no
  // memory for a new slot closes the current one: it answers nothing.
  enum class Fill : std::uint8_t { kEmpty, kReady, kClosed };
  struct ValueSlot {
    TableEpochs::PendingFree pendingFree;
    const std::uint64_t generation;
    std::atomic<Fill> fill{Fill::kEmpty};
    std::optional<V> value;

    explicit ValueSlot(std::uint64_t g) : generation(g) {}
  };

 public:
  ConcurrentValue() : _slot(new ValueSlot(0)) {}
  ~ConcurrentValue() { destroy(_slot.load()); }
  ConcurrentValue(const ConcurrentValue&) = delete;
  ConcurrentValue& operator=(const ConcurrentValue&) = delete;

  // Returns a copy; no reference into the slot escapes.
  template <class Provider>
  [[nodiscard]] V get(Provider&& provider) const {
    return useOrCompute(std::forward<Provider>(provider),
                        [](const V& value) { return value; });
  }

  // A lookup never computes. The optional distinguishes an absent answer
  // from a computed nullptr, and copies the value while its slot is guarded.
  [[nodiscard]] std::optional<V> find() const {
    const TableEpochs::Guard guard;
    const ValueSlot* const slot = _slot.load(std::memory_order_seq_cst);
    if (slot->fill.load(std::memory_order_acquire) == Fill::kReady)
      return *slot->value;
    return std::nullopt;
  }

  // The value is valid only during this call. The consumer returns void, so no
  // reference into the slot can escape the guard.
  template <class Provider, class Consumer>
  void withValue(Provider&& provider, Consumer&& consumer) const {
    static_assert(std::is_void_v<std::invoke_result_t<Consumer, const V&>>,
                  "a cache value consumer returns void");
    useOrCompute(std::forward<Provider>(provider),
                 std::forward<Consumer>(consumer));
  }

  // Never fails: with no memory for the next slot, the current one closes.
  void clear() noexcept {
    const TableEpochs::Guard guard;
    ValueSlot* previous = nullptr;
    {
      const std::lock_guard lock(_writers);
      previous = _slot.load(std::memory_order_seq_cst);
      auto* const next = new (std::nothrow) ValueSlot(previous->generation + 1);
      if (next == nullptr) {
        previous->fill.store(Fill::kClosed, std::memory_order_relaxed);
        return;
      }
      _slot.store(next, std::memory_order_seq_cst);
    }
    // A collection may run value destructors; no cache lock is held there.
    TableEpochs::freeLater(previous->pendingFree, previous, &destroy,
                           sizeof(ValueSlot));
  }

 private:
  template <class Provider, class Consumer>
  std::invoke_result_t<Consumer, const V&> useOrCompute(
      Provider&& provider, Consumer&& consumer) const {
    std::uint64_t generation;
    {
      const TableEpochs::Guard guard;
      const ValueSlot* const slot = _slot.load(std::memory_order_seq_cst);
      if (slot->fill.load(std::memory_order_acquire) == Fill::kReady)
        return std::invoke(std::forward<Consumer>(consumer), *slot->value);
      generation = slot->generation;
    }

    // Outside both the guard and lock: providers may reenter the cache, and
    // another provider or a clear need not wait for this computation.
    V computed = std::invoke(std::forward<Provider>(provider));
    {
      const TableEpochs::Guard guard;
      std::unique_lock lock(_writers);
      ValueSlot* const slot = _slot.load(std::memory_order_seq_cst);
      const Fill fill = slot->fill.load(std::memory_order_relaxed);
      if (slot->generation == generation && fill != Fill::kClosed) {
        // Moved in under the lock: V's move (or copy) must not use this cache.
        if (fill == Fill::kEmpty) {
          slot->value.emplace(std::move(computed));
          slot->fill.store(Fill::kReady, std::memory_order_release);
        }
        lock.unlock();
        // The guard keeps the value alive through the consumer, even if a clear
        // releases the slot.
        return std::invoke(std::forward<Consumer>(consumer),
                           std::as_const(*slot->value));
      }
    }
    // A clear came first: the caller gets its own answer, which is not
    // stored.
    return std::invoke(std::forward<Consumer>(consumer),
                       std::as_const(computed));
  }

  static void destroy(void* object) noexcept {
    delete static_cast<ValueSlot*>(object);
  }

  mutable std::atomic<ValueSlot*> _slot;
  mutable std::mutex _writers;
};

}  // namespace cursiv::detail
