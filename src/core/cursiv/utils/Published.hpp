#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <utility>
#include <vector>

// An immutable value read without a lock and replaced from time to time. A
// replaced value is freed once every reader stripe has moved past it.

namespace cursiv::detail {

// Not for a value written as often as it is read: each publish replaces the
// whole value.
template <class T>
class Published {
 private:
  // One line each, so thirty readers do not share one -- but two threads may
  // still hash to the same, so this counts depth and never parity.
  struct alignas(128) Stripe {
    std::atomic<std::uint32_t> active{0};
  };
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
          _value(std::exchange(other._value, nullptr)) {}
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

  // Replaces what readers see. The previous value is freed here or at a later
  // publication, once no reader can hold it.
  void publish(std::shared_ptr<const T> value) {
    // Released after the lock: a value's destructor may use this slot.
    std::vector<std::shared_ptr<const T>> freed;
    const std::lock_guard lock(_writers);
    const T* const raw = value.get();
    // Growing the release list can throw. Keep the published owner until the
    // list has successfully acquired its own copy.
    if (_live != nullptr) {
      _replaced.push_back(Replaced{_live, _replacements + 1});
      ++_replacements;
    }
    _live = std::move(value);
    // Stored before the stripes are read: the other half of the handshake, and
    // what makes an idle stripe below mean "holds the new value or none".
    _published.store(raw, std::memory_order_seq_cst);
    freed = collectLocked();
  }

  // Leaves readers with nothing, so the next one rebuilds.
  void clear() { publish(nullptr); }

  // How many replaced values are still held back. Diagnostic, and what a test
  // watches to know the reclamation runs at all.
  [[nodiscard]] std::size_t retainedCount() const {
    const std::lock_guard lock(_writers);
    return _replaced.size() - _oldest;
  }

 private:
  // Stable per thread and spread across threads, never unique.
  [[nodiscard]] Stripe& stripeForThisThread() const noexcept {
    return _stripes[std::hash<std::thread::id>{}(std::this_thread::get_id()) %
                    kStripes];
  }

  // `_writers` held. A value is free once every stripe has been seen idle
  // after its release: the oldest values first. Returns them to be released
  // after the lock; none if there is no room to hand them over.
  [[nodiscard]] std::vector<std::shared_ptr<const T>> collectLocked() noexcept {
    std::vector<std::shared_ptr<const T>> freed;
    if (_oldest == _replaced.size()) return freed;
    std::uint64_t covered = _replacements;
    for (std::size_t at = 0; at < kStripes; ++at) {
      if (_stripes[at].active.load(std::memory_order_seq_cst) == 0)
        _idleAfter[at] = _replacements;
      covered = std::min(covered, _idleAfter[at]);
    }
    std::size_t end = _oldest;
    while (end < _replaced.size() && _replaced[end].serial <= covered) ++end;
    if (end == _oldest) return freed;
    try {
      freed.reserve(end - _oldest);
    } catch (const std::bad_alloc&) {
      return freed;
    }
    for (; _oldest < end; ++_oldest)
      freed.push_back(std::move(_replaced[_oldest].value));
    // The freed front is erased once it is half the list, so an erase moves no
    // more than was freed since.
    if (_oldest == _replaced.size()) {
      _replaced.clear();
      _oldest = 0;
    } else if (2 * _oldest >= _replaced.size()) {
      _replaced.erase(_replaced.begin(),
                      _replaced.begin() + static_cast<std::ptrdiff_t>(_oldest));
      _oldest = 0;
    }
    return freed;
  }

  // A released value, and its rank among releases, from one.
  struct Replaced {
    std::shared_ptr<const T> value;
    std::uint64_t serial;
  };

  mutable std::array<Stripe, kStripes> _stripes;
  std::atomic<const T*> _published{nullptr};
  // Writers are rare -- one per invalidation -- so they may take a lock.
  mutable std::mutex _writers;
  std::shared_ptr<const T> _live;
  // Oldest first; those before `_oldest` are freed.
  std::vector<Replaced> _replaced;
  std::size_t _oldest = 0;
  std::uint64_t _replacements = 0;
  // Per stripe, the last release it has been seen idle after; none is 0.
  std::array<std::uint64_t, kStripes> _idleAfter{};
};

}  // namespace cursiv::detail
