#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "cursiv/execution/SpinLock.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"

namespace cursiv {

// Partitioned by document: one document's rebuild empties only its own
// entries, unlike `WorkspaceCache`, where any build empties everything.
// Without `state`, a changed or deleted document loses its entries when the
// update is announced, before the rebuild that answers it; with `state`, it
// loses them when it reaches that rung, deletions handled at once either way.
// Only `Linked` and `IndexedReferences` are accepted: an earlier rung could
// still be holding what an eviction would take, and `Validated`, the last
// rung, has nothing left to rebuild what it dropped.
//
// So a value may read its own document and, with `state`, the nodes that
// document's references resolve to: a change to one of them relinks it. A
// change further away rebuilds neither the document nor its entries, so a
// value that reads past those belongs in a `WorkspaceCache`.
template <class K, class V, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class DocumentCache : private DocumentPhaseListener,
                      private DocumentUpdateListener {
 public:
  explicit DocumentCache(SharedCoreServices& sharedServices,
                         std::optional<DocumentState> state = std::nullopt);
  ~DocumentCache() noexcept override;

  DocumentCache(const DocumentCache&) = delete;
  DocumentCache& operator=(const DocumentCache&) = delete;

  // The provider runs outside the lock. A clear or a deletion of the
  // document during computation, or a clear of the whole cache, keeps its
  // result out of the cache, but still returns it to the caller.
  template <class Provider>
  [[nodiscard]] V get(DocumentId document, const K& key,
                      Provider&& provider) const {
    if (document == kNoDocument)
      return std::invoke(std::forward<Provider>(provider));
    Shard& s = shardOf(document);
    Entry* entry = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t erasures = 0;
    {
      const std::scoped_lock lock(s.mutex);
      // Created on a miss, so a clear of the document during computation
      // finds an entry to mark.
      entry = &s.entries[document];
      if (const auto found = entry->values.find(key);
          found != entry->values.end())
        return found->second;
      generation = entry->generation;
      erasures = s.erasures;
    }
    V computed = std::invoke(std::forward<Provider>(provider));
    const std::scoped_lock lock(s.mutex);
    // With no erasure in the stripe since the miss, `entry` is still the
    // document's own.
    if (s.erasures != erasures || entry->generation != generation)
      return computed;
    return entry->values.try_emplace(key, std::move(computed)).first->second;
  }

  void clear(DocumentId document) {
    Shard& s = shardOf(document);
    Values dropped;  // freed once the lock is released
    const std::scoped_lock lock(s.mutex);
    if (const auto at = s.entries.find(document); at != s.entries.end()) {
      ++at->second.generation;
      // A swap exchanges the hash and the equality too, which not every pair
      // allows without throwing: theirs are freed under the lock.
      if constexpr (std::is_nothrow_swappable_v<Hash> &&
                    std::is_nothrow_swappable_v<KeyEqual>)
        dropped.swap(at->second.values);
      else
        at->second.values.clear();
    }
  }

  void clear() {
    for (Shard& s : _shards) {
      Entries dropped;  // freed once the lock is released
      const std::scoped_lock lock(s.mutex);
      ++s.erasures;
      dropped.swap(s.entries);
    }
  }

 private:
  void onDocumentPhase(Document& document, DocumentState reached,
                       const CancellationToken&) override {
    if (_state.has_value() && reached == *_state) clear(document.id);
  }

  void onUpdate(std::span<const std::string_view> changed,
                std::span<const std::string_view> deleted) override;

  // A deleted document never comes back under its id, so its entry goes
  // rather than waiting empty for one. Without an entry, the document has no
  // computation running either: each one starts by creating it.
  void erase(DocumentId document) {
    Shard& s = shardOf(document);
    typename Entries::node_type dropped;  // freed once the lock is released
    const std::scoped_lock lock(s.mutex);
    dropped = s.entries.extract(document);
    if (!dropped.empty()) ++s.erasures;
  }

  SharedCoreServices& _shared;
  DocumentBuilder* _builder = nullptr;
  std::optional<DocumentState> _state;

  // One document's values. `generation` counts its clears: a computation
  // that started under an earlier count stores nothing.
  using Values = std::unordered_map<K, V, Hash, KeyEqual>;
  struct Entry {
    std::uint64_t generation = 0;
    Values values;
  };
  using Entries = std::unordered_map<DocumentId, Entry>;

  // Striped by document: one lock for the whole map would serialise every
  // document a parallel build reads at once. A document lives in exactly one
  // stripe, so every per-document call takes one lock; only `clear()` walks
  // them all. The lock is held for lookups and an insertion only, never while
  // values are freed, so a spin lock suits it. Each stripe has lines of its
  // own, or neighbouring documents read on different threads would contend
  // through them. `erasures` counts the entries the stripe removed: an entry
  // removed and then created again starts its generation over, so a
  // computation that saw a removal stores nothing either. Erasures are rare,
  // so they cost the stripe's other documents little.
  static constexpr std::size_t kStripes = 64;
  struct alignas(128) Shard {
    mutable detail::SpinLock mutex;
    std::uint64_t erasures = 0;
    Entries entries;
  };
  [[nodiscard]] Shard& shardOf(DocumentId document) const {
    return _shards[stripe(document)];
  }
  [[nodiscard]] static std::size_t stripe(DocumentId document) {
    return static_cast<std::uint32_t>(document) % kStripes;
  }
  mutable std::array<Shard, kStripes> _shards;
};

template <class K, class V, class Hash, class KeyEqual>
DocumentCache<K, V, Hash, KeyEqual>::DocumentCache(
    SharedCoreServices& sharedServices, std::optional<DocumentState> state)
    : _shared(sharedServices),
      _builder(sharedServices.documentBuilder.get()),
      _state(state) {
  if (state.has_value() && *state != DocumentState::Linked &&
      *state != DocumentState::IndexedReferences)
    throw std::invalid_argument(
        "cursiv: a DocumentCache evicts on Linked or IndexedReferences");
  if (_builder == nullptr) return;
  if (state.has_value()) _builder->addDocumentPhaseListener(*this);
  try {
    _builder->addDocumentUpdateListener(*this);
  } catch (...) {
    if (state.has_value()) _builder->removeDocumentPhaseListener(*this);
    throw;
  }
}

template <class K, class V, class Hash, class KeyEqual>
DocumentCache<K, V, Hash, KeyEqual>::~DocumentCache() noexcept {
  if (_builder == nullptr) return;
  if (_state.has_value()) _builder->removeDocumentPhaseListener(*this);
  _builder->removeDocumentUpdateListener(*this);
}

// The update speaks URIs; the partition is by id, so the store translates. A
// URI it does not know names nothing to evict.
template <class K, class V, class Hash, class KeyEqual>
void DocumentCache<K, V, Hash, KeyEqual>::onUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted) {
  const Documents& store = *_shared.documents;
  // A rung-based cache still drops a deleted document at once: it reaches no
  // rung to be evicted at.
  if (!_state.has_value())
    for (const std::string_view uri : changed)
      if (const DocumentId id = store.getDocumentId(uri); id != kNoDocument)
        clear(id);
  for (const std::string_view uri : deleted)
    if (const DocumentId id = store.getDocumentId(uri); id != kNoDocument)
      erase(id);
}

}  // namespace cursiv
