#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/Documents.hpp"

namespace cursiv {

// Partitioned by document: one document's rebuild empties only its own
// entries, unlike `WorkspaceCache`, where any build empties everything.
// Without `state`, a changed or deleted document loses its entries when the
// update is announced, before the rebuild that answers it; with `state`, it
// loses them when it reaches that rung, deletions handled at once either way.
// Only `Linked` and `IndexedReferences` are accepted: an earlier rung could
// still be holding what an eviction would take, and `Validated`, the last
// rung, has nothing left to rebuild what it dropped.
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

  // The provider runs outside the lock. An eviction during computation keeps
  // its result out of the cache, but still returns it to the caller.
  template <class Provider>
  [[nodiscard]] V get(DocumentId document, const K& key,
                      Provider&& provider) const {
    if (document == kNoDocument)
      return std::invoke(std::forward<Provider>(provider));
    Shard& s = shardOf(document);
    std::uint64_t seen = 0;
    {
      const std::shared_lock lock(s.mutex);
      if (const auto at = s.entries.find(document); at != s.entries.end())
        if (const auto found = at->second.find(key); found != at->second.end())
          return found->second;
      seen = s.generation;
    }
    V computed = std::invoke(std::forward<Provider>(provider));
    const std::unique_lock lock(s.mutex);
    if (s.generation != seen) return computed;
    return s.entries[document]
        .try_emplace(key, std::move(computed))
        .first->second;
  }

  void clear(DocumentId document) {
    Shard& s = shardOf(document);
    const std::unique_lock lock(s.mutex);
    ++s.generation;
    s.entries.erase(document);
  }

  void clear() {
    for (Shard& s : _shards) {
      const std::unique_lock lock(s.mutex);
      ++s.generation;
      s.entries.clear();
    }
  }

 private:
  void onDocumentPhase(Document& document, DocumentState reached,
                       const CancellationToken&) override {
    if (_state.has_value() && reached == *_state) clear(document.id);
  }

  void onUpdate(std::span<const std::string_view> changed,
                std::span<const std::string_view> deleted) override;

  SharedCoreServices& _shared;
  DocumentBuilder* _builder = nullptr;
  std::optional<DocumentState> _state;

  // Striped by document: one mutex for the whole map would serialise every
  // document a parallel build indexes at once. A document lives in exactly
  // one stripe, so every per-document call takes one uncontended lock; only
  // `clear()` walks them all. The generation is per stripe for the same
  // reason: it guards a value against its own document being evicted.
  static constexpr std::size_t kStripes = 64;
  struct Shard {
    mutable std::shared_mutex mutex;
    std::uint64_t generation = 0;
    std::unordered_map<DocumentId, std::unordered_map<K, V, Hash, KeyEqual>>
        entries;
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
  const auto evict = [&](std::span<const std::string_view> uris) {
    for (const std::string_view uri : uris)
      if (const DocumentId id = store.getDocumentId(uri); id != kNoDocument)
        clear(id);
  };
  // A rung-based cache still drops a deleted document at once: it reaches no
  // rung to be evicted at.
  if (!_state.has_value()) evict(changed);
  evict(deleted);
}

}  // namespace cursiv
