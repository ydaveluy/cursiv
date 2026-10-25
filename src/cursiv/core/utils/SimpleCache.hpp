#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "cursiv/core/utils/Published.hpp"

namespace cursiv {

// Concurrent key/value memoisation, safe to call from any thread. `get(key,
// provider)` is the only door: a `set` or `erase` would let a caller put the
// cache out of step with the workspace it describes. `V` must outlive the
// entry -- a cached `string_view` dangles once `clear` runs.
template <class K, class V, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class SimpleCache {
 public:
  virtual ~SimpleCache() noexcept = default;

  // A hit on a published entry takes no lock, cheaper than a reader-writer
  // lock for one workspace-wide question per AST node. The table readers see
  // is replaced once the entries added or read behind it have paid for the
  // copy, so a fill is linear; until then such an entry is read under the
  // lock. Returns a copy, never a reference into the table another thread may
  // be rehashing.
  template <class Provider>
  [[nodiscard]] V get(const K& key, Provider&& provider) const {
    if (const auto table = _published.read(); table)
      if (const auto at = table->find(key); at != table->end())
        return at->second;
    std::uint64_t seen = 0;
    {
      const std::shared_lock lock(_mutex);
      if (const auto at = _live.find(key); at != _live.end()) {
        V found = at->second;
        publishIfOwed();
        return found;
      }
      seen = _generation;
    }
    // Runs outside the lock, so it may race another thread on the same
    // absent key; the loser's result is discarded for the first one
    // published, so a provider must be deterministic per key.
    V computed = std::invoke(std::forward<Provider>(provider));

    const std::unique_lock lock(_mutex);
    // A `clear` that ran while the provider did means this value describes a
    // workspace that's gone -- hand it back to the caller, but don't publish
    // it, or the next reader gets an answer about the previous build.
    if (_generation != seen) return computed;
    if (const auto at = _live.find(key); at != _live.end()) return at->second;
    V result = _live.try_emplace(key, std::move(computed)).first->second;
    publishIfOwed();
    return result;
  }

  // Public because the listeners of the two derived caches call it directly;
  // nothing outside asks.
  virtual void clear() {
    const std::unique_lock lock(_mutex);
    ++_generation;
    _live.clear();
    _owed = 0;
    _published.retire();
  }

 private:
  using Table = std::unordered_map<K, V, Hash, KeyEqual>;

  // `_mutex` held, shared or not. Each entry added or read from `_live` pays
  // for an eighth of the table: the copy is owed once they add up to it.
  void publishIfOwed() const {
    const std::size_t paid = _owed.fetch_add(1, std::memory_order_relaxed) + 1;
    if (paid < std::max<std::size_t>(1, _live.size() / 8)) return;
    if (_owed.exchange(0, std::memory_order_relaxed) != 0)
      _published.publish(std::make_shared<const Table>(_live));
  }

  mutable std::shared_mutex _mutex;
  // Bumped by every removal. It is what lets a provider run outside the lock
  // without a value outliving the workspace it describes.
  mutable std::uint64_t _generation = 0;
  // The writer's copy, and the immutable one readers see.
  mutable Table _live;
  mutable Published<Table> _published;
  mutable std::atomic<std::size_t> _owed{0};
};

}  // namespace cursiv
