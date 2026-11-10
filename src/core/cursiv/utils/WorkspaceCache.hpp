#pragma once

#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/SimpleCache.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentState.hpp"

namespace cursiv {

struct Document;

// A cache for the whole workspace, emptied by every build: at the update, or,
// with `state`, once its listeners hear that state; a read that waited for it
// may still see the old value. Pass the state the provider reads, such as
// `Linked` for a cache read by a validator.
//
// WorkspaceCache<V> holds one value, without a key. get(provider) returns a
// copy; withValue(provider, consumer) passes a reference that must not outlive
// the consumer.
template <class K, class V = void, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class WorkspaceCache : public SimpleCache<K, V, Hash, KeyEqual>,
                       private BuildPhaseListener,
                       private DocumentUpdateListener {
 public:
  // Deleting a document always empties the cache at once.
  explicit WorkspaceCache(SharedCoreServices& sharedServices,
                          std::optional<DocumentState> state = std::nullopt);
  ~WorkspaceCache() noexcept override;

  WorkspaceCache(const WorkspaceCache&) = delete;
  WorkspaceCache& operator=(const WorkspaceCache&) = delete;

 private:
  void onBuildPhase(DocumentState reached, std::span<Document* const>,
                    const CancellationToken&) override {
    if (_state.has_value() && reached == *_state) this->clear();
  }

  // A deleted document reaches no state, so it empties the cache here.
  void onUpdate(std::span<const std::string_view>,
                std::span<const std::string_view> deleted) override {
    if (!_state.has_value() || !deleted.empty()) this->clear();
  }

  DocumentBuilder* _builder = nullptr;
  std::optional<DocumentState> _state;
};

template <class K, class V, class Hash, class KeyEqual>
WorkspaceCache<K, V, Hash, KeyEqual>::WorkspaceCache(
    SharedCoreServices& sharedServices, std::optional<DocumentState> state)
    : _builder(sharedServices.documentBuilder.get()), _state(state) {
  // `Changed` is never published: a cache waiting for it would never empty.
  if (state == DocumentState::Changed)
    throw std::invalid_argument(
        "cursiv: a WorkspaceCache state must be one a build reaches, not "
        "Changed");
  if (_builder == nullptr) return;
  _builder->addBuildPhaseListener(*this);
  try {
    _builder->addDocumentUpdateListener(*this);
  } catch (...) {
    _builder->removeBuildPhaseListener(*this);
    throw;
  }
}

template <class K, class V, class Hash, class KeyEqual>
WorkspaceCache<K, V, Hash, KeyEqual>::~WorkspaceCache() noexcept {
  if (_builder == nullptr) return;
  _builder->removeBuildPhaseListener(*this);
  _builder->removeDocumentUpdateListener(*this);
}

}  // namespace cursiv
