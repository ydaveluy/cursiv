#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "cursiv/utils/ConcurrentTable.hpp"

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

  // A hit takes no lock and writes nothing shared, so a workspace-wide
  // question asked by every thread costs each a lookup and nothing more.
  // Returns a copy, never a reference into a table a `clear` may free.
  template <class Provider>
  [[nodiscard]] V get(const K& key, Provider&& provider) const {
    if (std::optional<V> found = _values.find(key)) return std::move(*found);
    const std::uint64_t generation = _values.generation();
    // Runs outside any lock, so it may race another thread on the same
    // absent key; the first value stored wins and the other is discarded, so
    // a provider must be deterministic per key. A `clear` that ran while it
    // did means this value describes a workspace that's gone: the caller gets
    // it, the next reader does not.
    V computed = std::invoke(std::forward<Provider>(provider));
    if (std::optional<V> kept = _values.insert(generation, key, computed))
      return std::move(*kept);
    return computed;
  }

  // Public because the listeners of `WorkspaceCache` call it directly;
  // nothing outside asks. The table keeps its room: a cleared cache is
  // filled again by the next build.
  virtual void clear() { _values.clear(true); }

 private:
  mutable detail::ConcurrentTable<K, V, Hash, KeyEqual> _values;
};

}  // namespace cursiv
