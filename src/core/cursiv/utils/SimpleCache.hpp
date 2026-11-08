#pragma once

#include <functional>
#include <utility>

#include "cursiv/utils/ConcurrentTable.hpp"
#include "cursiv/utils/ConcurrentValue.hpp"

namespace cursiv {

// SimpleCache<Value> memoises one answer; SimpleCache<Key, Value> stores one
// per key, with optional Hash and KeyEqual types for that keyed form.
// Concurrent key/value memoisation, safe to call from any thread. `get(key,
// provider)` is the only door: a `set` or `erase` would let a caller put the
// cache out of step with the workspace it describes. `V` must outlive the
// entry -- a cached `string_view` dangles once `clear` runs. `Hash` and
// `KeyEqual` are built anew for each call, so every instance must give the
// same answer, and `Hash` must not throw.
template <class K, class V = void, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class SimpleCache {
 public:
  virtual ~SimpleCache() noexcept = default;

  // A hit takes no lock and writes nothing shared, so a workspace-wide
  // question asked by every thread costs each a lookup and nothing more.
  // Returns a copy, never a reference into a table a `clear` may free.
  //
  // The provider runs outside any lock, so it may race another thread on the
  // same absent key; the first value stored wins and the other is discarded,
  // so a provider must be deterministic per key. A `clear` that ran while it
  // did means this value describes a workspace that's gone: the caller gets
  // it, the next reader does not.
  template <class Provider>
  [[nodiscard]] V get(const K& key, Provider&& provider) const {
    return _values.findOrCompute(key, std::forward<Provider>(provider));
  }

  // Public because the listeners of `WorkspaceCache` call it directly;
  // nothing outside asks. The table keeps its room: a cleared cache is
  // filled again by the next build.
  virtual void clear() { _values.clear(true); }

 private:
  mutable detail::ConcurrentTable<K, V, Hash, KeyEqual> _values;
};

// A memo with exactly one answer needs no fictional key. `get(provider)`
// copies it out; `withValue(provider, consumer)` lends a const reference only
// through the consumer call, safe even if another thread clears the cache.
// References, pointers or views into the slot must not outlive that call.
// Providers have the keyed cache's contract: no lock is held while they run,
// first publication wins, and a clear prevents stale publication.
template <class V, class Hash, class KeyEqual>
class SimpleCache<V, void, Hash, KeyEqual> {
 public:
  virtual ~SimpleCache() noexcept = default;

  template <class Provider>
  [[nodiscard]] V get(Provider&& provider) const {
    return _value.get(std::forward<Provider>(provider));
  }

  template <class Provider, class Consumer>
  void withValue(Provider&& provider, Consumer&& consumer) const {
    _value.withValue(std::forward<Provider>(provider),
                     std::forward<Consumer>(consumer));
  }

  virtual void clear() { _value.clear(); }

 private:
  mutable detail::ConcurrentValue<V> _value;
};

}  // namespace cursiv
