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

// A cache partitioned by document: a rebuild empties only that document's
// entries, at the update or, with `state` (`Linked` or `IndexedReferences`),
// once its listeners hear that state; a read that waited for it may still see
// the old ones. A value may read its own document and, with `state`, its
// references' targets; one that reads further belongs in a `WorkspaceCache`.
//
// DocumentCache<V> holds one value per document. get(document, provider)
// returns a copy, get(document) only looks it up, and withValue(document,
// provider, consumer) passes the consumer a reference it must not keep.
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

  // The provider runs outside any lock, and a hit takes none. A clear or a
  // deletion during the computation keeps its result out of the cache; the
  // caller still gets it.
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
    const Values* const values = find_values(document);
    return values != nullptr ? values->find(key) : std::nullopt;
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
    const Values* const values = find_values(document);
    return values != nullptr ? values->find() : std::nullopt;
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

  struct Last {
    std::uint64_t serial = 0;
    DocumentId document = kNoDocument;
    Values* values = nullptr;
  };
  // The partition of `document`, or null: a lookup makes none.
  [[nodiscard]] Values* find_values(DocumentId document) const {
    // The document this thread read last from a cache of this type. The serial
    // tells this cache from one built later at the same address.
    thread_local Last last;
    if (last.serial == _serial && last.document == document) [[likely]]
      return last.values;
    const std::optional<Values*> found = _documents.find(document);
    if (!found) return nullptr;
    last = Last{_serial, document, *found};
    return *found;
  }

  // Made on the first read with a provider, and kept: a clear empties a
  // partition in place.
  [[nodiscard]] Values& valuesOf(DocumentId document) const {
    if (Values* const found = find_values(document)) [[likely]]
      return *found;
    Values* values = nullptr;
    {
      const std::scoped_lock lock(_creating);
      const std::optional<Values*> found = _documents.find(document);
      values = found ? *found : &_owned.emplace_back();
    }
    // Stored with no lock held: the insert's growth may free a value that reads
    // this cache. Of partitions made at once, the first stored is kept and the
    // others stay unused until the cache goes.
    const std::optional<Values*> kept = _documents.insert(0, document, values);
    assert(kept.has_value() && "the document table is never cleared");
    return **kept;
  }
  [[nodiscard]] static std::uint64_t next_serial() noexcept {
    static std::atomic<std::uint64_t> serials{0};
    return serials.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  SharedCoreServices& _shared;
  DocumentBuilder* _builder = nullptr;
  std::optional<DocumentState> _state;
  const std::uint64_t _serial = next_serial();

  // One stable partition per document read with a provider; a deleted
  // document's stays empty. Kept where nothing moves them, and found by id.
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

// The update gives URIs; the partition is by id, so the store translates. A
// URI it does not know names nothing to evict.
template <class K, class V, class Hash, class KeyEqual>
void DocumentCache<K, V, Hash, KeyEqual>::onUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted) {
  const Documents& store = *_shared.documents;
  // A cache with a state still drops a deleted document at once: it reaches
  // no state.
  if (!_state.has_value())
    for (const std::string_view uri : changed)
      if (const DocumentId id = store.getDocumentId(uri); id != kNoDocument)
        clear(id);
  for (const std::string_view uri : deleted)
    if (const DocumentId id = store.getDocumentId(uri); id != kNoDocument)
      clear(id, false);
}

}  // namespace cursiv
