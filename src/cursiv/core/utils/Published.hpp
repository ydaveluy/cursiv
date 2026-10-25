#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// An immutable value read without a lock, replaced from time to time: a
// reader raises a stripe before loading the pointer, a publisher stores the
// pointer before reading the stripes, so a retired value frees only once
// every stripe has moved on.

namespace cursiv {

// Not for a value written as often as it is read: publishing hands over a
// whole new value, so a per-symbol memo belongs behind sharded locks instead.
template <class T>
class Published {
 private:
  // One line each, so thirty readers do not share one -- but two threads may
  // still hash to the same, so this counts depth and never parity.
  struct alignas(128) Stripe {
    std::atomic<std::uint32_t> active{0};
  };
  // Sixty-four exactly: `Retired::owed` is one word with a bit per stripe.
  static constexpr std::size_t kStripes = 64;

 public:
  Published() = default;
  Published(const Published&) = delete;
  Published& operator=(const Published&) = delete;

  // A reader's handle: the value lives exactly as long as it does. A pointer
  // or span kept past its destruction outlives its guarantee.
  class Read {
   public:
    explicit Read(const Published& owner) noexcept
        : _stripe(&owner.stripeForThisThread()) {
      // Raised before the load below, sequentially consistent so it cannot
      // sink past it. A publisher that reads this stripe as idle has already
      // stored the pointer this load will see.
      _stripe->active.fetch_add(1, std::memory_order_seq_cst);
      // This load joins the same total order as the publisher's store and
      // stripe scan. Acquire alone does not constrain which value it reads
      // relative to sequentially consistent operations on another atomic.
      _value = owner._published.load(std::memory_order_seq_cst);
    }
    ~Read() noexcept {
      if (_stripe != nullptr)
        _stripe->active.fetch_sub(1, std::memory_order_release);
    }

    Read(Read&& other) noexcept
        : _stripe(std::exchange(other._stripe, nullptr)),
          _value(other._value) {}
    Read& operator=(Read&&) = delete;
    Read(const Read&) = delete;
    Read& operator=(const Read&) = delete;

    [[nodiscard]] const T* get() const noexcept { return _value; }
    [[nodiscard]] const T* operator->() const noexcept { return _value; }
    [[nodiscard]] const T& operator*() const noexcept { return *_value; }
    explicit operator bool() const noexcept { return _value != nullptr; }

   private:
    Stripe* _stripe;
    const T* _value = nullptr;
  };

  [[nodiscard]] Read read() const noexcept { return Read(*this); }

  // Replaces what readers see. The previous value is held until no reader can
  // be inside it, which is tested here and again at the next publication.
  void publish(std::shared_ptr<const T> value) {
    const std::lock_guard lock(_writers);
    const T* const raw = value.get();
    // Growing the retire list can throw. Keep the published owner until the
    // list has successfully acquired its own copy.
    if (_live != nullptr) _retired.push_back(Retired{_live, ~0ULL});
    _live = std::move(value);
    // Stored before the stripes are read: the other half of the handshake, and
    // what makes an idle stripe below mean "holds the new value or none".
    _published.store(raw, std::memory_order_seq_cst);
    collectLocked();
  }

  // Leaves readers with nothing, so the next one rebuilds.
  void retire() { publish(nullptr); }

  // How many replaced values are still held back. Diagnostic, and what a test
  // watches to know the reclamation runs at all.
  [[nodiscard]] std::size_t retainedCount() const {
    const std::lock_guard lock(_writers);
    return _retired.size();
  }

 private:
  // Stable per thread and spread across threads, never unique.
  [[nodiscard]] Stripe& stripeForThisThread() const noexcept {
    return _stripes[std::hash<std::thread::id>{}(std::this_thread::get_id()) %
                    kStripes];
  }

  // `_writers` held. A stripe seen idle at any moment past a value's retirement
  // owes it nothing more: every reader that could hold it entered before the
  // publication and has left. Each value keeps its own set of stripes still
  // owed, so they need never be idle at the same instant -- which, with readers
  // in flight, is a moment that does not come.
  void collectLocked() const {
    if (_retired.empty()) return;
    std::uint64_t idle = 0;
    for (std::size_t at = 0; at < kStripes; ++at)
      if (_stripes[at].active.load(std::memory_order_seq_cst) == 0)
        idle |= std::uint64_t{1} << at;
    if (idle == 0) return;
    std::erase_if(_retired, [idle](Retired& retired) {
      retired.owed &= ~idle;
      return retired.owed == 0;
    });
  }

  // A retired value and the stripes that have not yet been seen idle under it.
  struct Retired {
    std::shared_ptr<const T> value;
    std::uint64_t owed;
  };

  mutable std::array<Stripe, kStripes> _stripes;
  std::atomic<const T*> _published{nullptr};
  // Writers are rare -- one per invalidation -- so they may take a lock.
  mutable std::mutex _writers;
  std::shared_ptr<const T> _live;
  mutable std::vector<Retired> _retired;
};

}  // namespace cursiv
