#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include "cursiv/utils/ConcurrentTable.hpp"

namespace cursiv::detail {

// A workspace question with no key dimension. Values are immutable once
// published. Like ConcurrentTable, every borrowed read holds the epoch until
// its consumer returns, including when a concurrent clear retires the slot.
template <class V>
class ConcurrentValue {
  struct ValueSlot {
    TableEpochs::Retired retired;
    const std::uint64_t generation;
    std::atomic<bool> ready{false};
    std::optional<V> value;

    explicit ValueSlot(std::uint64_t g) : generation(g) {}
  };

 public:
  ConcurrentValue() : _slot(new ValueSlot(0)) {}
  ~ConcurrentValue() { destroy(_slot.load()); }
  ConcurrentValue(const ConcurrentValue&) = delete;
  ConcurrentValue& operator=(const ConcurrentValue&) = delete;

  // Copies out, exactly as the keyed cache does. No reference into the slot
  // escapes.
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
    if (slot->ready.load(std::memory_order_acquire)) return *slot->value;
    return std::nullopt;
  }

  // Borrowed only for this call. The consumer returns void so the API cannot
  // accidentally return a reference into a slot its guard is about to leave.
  template <class Provider, class Consumer>
  void withValue(Provider&& provider, Consumer&& consumer) const {
    static_assert(std::is_void_v<std::invoke_result_t<Consumer, const V&>>,
                  "a cache value consumer returns void");
    useOrCompute(std::forward<Provider>(provider),
                 std::forward<Consumer>(consumer));
  }

  void clear() {
    const TableEpochs::Guard guard;
    ValueSlot* previous;
    {
      const std::lock_guard lock(_writers);
      previous = _slot.load(std::memory_order_seq_cst);
      auto next = std::make_unique<ValueSlot>(previous->generation + 1);
      _slot.store(next.release(), std::memory_order_seq_cst);
    }
    // A collection may run value destructors; no cache lock is held there.
    TableEpochs::retire(previous->retired, previous, &destroy,
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
      if (slot->ready.load(std::memory_order_acquire))
        return std::invoke(std::forward<Consumer>(consumer), *slot->value);
      generation = slot->generation;
    }

    // Outside both the guard and lock: providers may reenter the cache, and
    // another provider or a clear need not wait for this computation.
    V computed = std::invoke(std::forward<Provider>(provider));
    const TableEpochs::Guard guard;
    std::unique_lock lock(_writers);
    ValueSlot* const slot = _slot.load(std::memory_order_seq_cst);
    if (slot->generation != generation) {
      lock.unlock();
      // The caller gets its computed answer, but this generation cannot
      // publish a result from the workspace a clear has already replaced.
      return std::invoke(std::forward<Consumer>(consumer),
                         std::as_const(computed));
    }
    if (!slot->ready.load(std::memory_order_relaxed)) {
      slot->value.emplace(std::move(computed));
      slot->ready.store(true, std::memory_order_release);
    }
    lock.unlock();
    // A clear after unlocking may retire slot; the guard keeps its value
    // alive throughout this consumer, without copying an owning V per hit.
    return std::invoke(std::forward<Consumer>(consumer),
                       std::as_const(*slot->value));
  }

  static void destroy(void* object) noexcept {
    delete static_cast<ValueSlot*>(object);
  }

  mutable std::atomic<ValueSlot*> _slot;
  mutable std::mutex _writers;
};

}  // namespace cursiv::detail
