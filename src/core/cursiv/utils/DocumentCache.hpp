#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/ConcurrentTable.hpp"
#include "cursiv/utils/ConcurrentValue.hpp"
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
//
// A document read once keeps a small empty partition after deletion: a value
// slot, or a table of under a kilobyte for keyed entries. The document store
// itself keeps a row for each URI it has seen.
// `Hash` and `KeyEqual` are as `SimpleCache` takes them.
//
// DocumentCache<Value> stores one answer per document, without a secondary key:
// get(document, provider) returns a copy, get(document) only looks it up,
// and withValue(document, provider, consumer) borrows it through the consumer
// call, retaining it even while a clear retires it. References, pointers or
// views into that slot must not outlive the consumer. DocumentCache<Key, Value>
// keeps the keyed interface.
template <class K, class V = void, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class DocumentCache : private DocumentPhaseListener,
                      private DocumentUpdateListener {
  using Value = std::conditional_t<std::is_void_v<V>, K, V>;

 public:
  explicit DocumentCache(SharedCoreServices& sharedServices,
                         std::optional<DocumentState> state = std::nullopt);
  ~DocumentCache() noexcept override;

  DocumentCache(const DocumentCache&) = delete;
  DocumentCache& operator=(const DocumentCache&) = delete;

  // The provider runs outside any lock, and a hit takes none either: a
  // reader writes nothing shared, so any number of threads read one document
  // at once. A clear or a deletion of the document during computation, or a
  // clear of the whole cache, keeps its result out of the cache, but still
  // returns it to the caller.
  template <class Provider>
  [[nodiscard]] Value get(DocumentId document, const K& key,
                          Provider&& provider) const
    requires(!std::is_void_v<V>)
  {
    if (document == kNoDocument)
      return std::invoke(std::forward<Provider>(provider));
    return valuesOf(document).findOrCompute(key,
                                            std::forward<Provider>(provider));
  }

  // What the cache holds for `key` now, if anything: nothing is computed. A
  // walk that stores an answer for each step it passes reads its way to the
  // first step already answered.
  [[nodiscard]] std::optional<Value> get(DocumentId document,
                                         const K& key) const
    requires(!std::is_void_v<V>)
  {
    if (document == kNoDocument) return std::nullopt;
    return valuesOf(document).find(key);
  }

  template <class Provider>
  [[nodiscard]] Value get(DocumentId document, Provider&& provider) const
    requires(std::is_void_v<V>)
  {
    if (document == kNoDocument)
      return std::invoke(std::forward<Provider>(provider));
    return valuesOf(document).get(std::forward<Provider>(provider));
  }

  [[nodiscard]] std::optional<Value> get(DocumentId document) const
    requires(std::is_void_v<V>)
  {
    if (document == kNoDocument) return std::nullopt;
    return valuesOf(document).find();
  }

  template <class Provider, class Consumer>
  void withValue(DocumentId document, Provider&& provider,
                 Consumer&& consumer) const
    requires(std::is_void_v<V>)
  {
    if (document == kNoDocument) {
      const Value computed = std::invoke(std::forward<Provider>(provider));
      std::invoke(std::forward<Consumer>(consumer), computed);
      return;
    }
    valuesOf(document).withValue(std::forward<Provider>(provider),
                                 std::forward<Consumer>(consumer));
  }

  // Emptied for a rebuild, the document keeps the room its entries took.
  void clear(DocumentId document) { clear(document, true); }

  void clear() {
    _documents.forEach([](Values* values) { values->clear(); });
  }

 private:
  void onDocumentPhase(Document& document, DocumentState reached,
                       const CancellationToken&) override {
    if (_state.has_value() && reached == *_state) clear(document.id);
  }

  void onUpdate(std::span<const std::string_view> changed,
                std::span<const std::string_view> deleted) override;

  // A deleted document may never come back: it keeps no room.
  void clear(DocumentId document, bool keepCapacity) {
    if (const std::optional<Values*> values = _documents.find(document)) {
      if constexpr (std::is_void_v<V>)
        (*values)->clear();
      else
        (*values)->clear(keepCapacity);
    }
  }

  using Values = std::conditional_t<
      std::is_void_v<V>, detail::ConcurrentValue<Value>,
      detail::ConcurrentTable<K, V, Hash, KeyEqual>>;
  struct DocumentHash {
    [[nodiscard]] std::size_t operator()(DocumentId document) const noexcept {
      return static_cast<std::uint32_t>(document);
    }
  };

  // Created on the first read of the document, and kept: a clear empties a
  // document's table in place, so a computation never outlives the table it
  // will store into.
  [[nodiscard]] Values& valuesOf(DocumentId document) const {
    // The document read last on this thread, by this cache: a validation
    // reads one document thousands of times in a row. A table lives as long
    // as its cache, so the pointer holds; the serial tells this cache from
    // one built since at the same address.
    thread_local Last last;
    if (last.serial == _serial && last.document == document) [[likely]]
      return *last.values;
    Values& values = find_or_create(document);
    last = Last{_serial, document, &values};
    return values;
  }
  // A miss takes the lock, so two readers of a new document make one table.
  [[nodiscard]] Values& find_or_create(DocumentId document) const {
    if (const std::optional<Values*> found = _documents.find(document))
      return **found;
    const std::scoped_lock lock(_creating);
    if (const std::optional<Values*> found = _documents.find(document))
      return **found;
    Values* created = &_owned.emplace_back();
    [[maybe_unused]] const std::optional<Values*> kept =
        _documents.insert(0, document, created);
    assert(kept == created && "the document table is never cleared");
    return *created;
  }

  struct Last {
    std::uint64_t serial = 0;
    DocumentId document = kNoDocument;
    Values* values = nullptr;
  };
  [[nodiscard]] static std::uint64_t next_serial() noexcept {
    static std::atomic<std::uint64_t> serials{0};
    return serials.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  SharedCoreServices& _shared;
  DocumentBuilder* _builder = nullptr;
  std::optional<DocumentState> _state;
  const std::uint64_t _serial = next_serial();

  // One stable partition per document ever read; a deleted document's stays
  // empty. Kept where nothing moves them, and found by id.
  mutable std::deque<Values> _owned;
  mutable std::mutex _creating;
  mutable detail::ConcurrentTable<DocumentId, Values*, DocumentHash> _documents;
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
      clear(id, false);
}

}  // namespace cursiv
