#pragma once

#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/SimpleCache.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"

namespace cursiv {

struct Document;

// Scoped to the whole workspace: any build empties it. `state` picks the
// rung to evict on -- a cache read by a validator is emptied when linking
// finishes, not when validation does, or it would be thrown away between the
// two halves of the pass that fills it. Without `state` the eviction comes at
// the announcement, ahead of the rebuild: an answer computed while the
// workspace is half built stays until the next update, so a provider that
// reads a document's state names the rung it needs.
template <class K, class V, class Hash = std::hash<K>,
          class KeyEqual = std::equal_to<K>>
class WorkspaceCache : public SimpleCache<K, V, Hash, KeyEqual>,
                       private BuildPhaseListener,
                       private DocumentUpdateListener {
 public:
  // Without `state`, any announced update empties the cache; with one, the
  // eviction waits for that rung -- deletions still empty at once.
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

  // A deleted document reaches no phase, so the rung above never fires for
  // it; a cache with no rung at all empties on every announcement.
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
        "cursiv: a WorkspaceCache evicts on a rung a build publishes");
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
