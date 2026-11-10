#pragma once

#include <functional>
#include <utility>

#include "cursiv/utils/ConcurrentTable.hpp"
#include "cursiv/utils/ConcurrentValue.hpp"

namespace cursiv {

// A thread-safe memo: SimpleCache<Key, Value> keeps one answer per key,
// SimpleCache<Value> a single answer. Values must not point into the cache
// (a cached `string_view` dangles after `clear`); `Hash` must not throw.
template <class K, class V = void, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class SimpleCache {
 public:
  virtual ~SimpleCache() noexcept = default;

  // Returns a copy of the answer for `key`, computed by `provider` on a
  // miss. The provider runs without a lock and may race: it must give the
  // same answer for the same key.
  template <class Provider>
  [[nodiscard]] V get(const K& key, Provider&& provider) const {
    return _values.findOrCompute(key, std::forward<Provider>(provider));
  }

  // Empties the cache and keeps its room for the next build.
  virtual void clear() { _values.clear(true); }

 private:
  mutable detail::ConcurrentTable<K, V, Hash, KeyEqual> _values;
};

// One answer, without a key. `get(provider)` returns a copy;
// `withValue(provider, consumer)` passes a reference that must not outlive the
// consumer. V's move must not use this cache.
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
