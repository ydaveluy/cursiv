#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <pegium/core/services/DefaultSharedCoreService.hpp>
#include <pegium/core/workspace/DocumentBuilder.hpp>

namespace pegium::workspace {

/// Default document builder driving parse, index, link, and validation phases.
class DefaultDocumentBuilder : public DocumentBuilder,
                               protected pegium::DefaultSharedCoreService {
public:
  explicit DefaultDocumentBuilder(
      const pegium::SharedCoreServices &sharedServices);

  [[nodiscard]] BuildOptions &updateBuildOptions() noexcept override;
  [[nodiscard]] const BuildOptions &
  updateBuildOptions() const noexcept override;

  void build(std::span<const std::shared_ptr<Document>> documents,
             const BuildOptions &options = {},
             utils::CancellationToken cancelToken = {},
             const std::function<void()> &downgradeLock = {}) const override;

  void update(std::span<const DocumentId> changedDocumentIds,
              std::span<const DocumentId> deletedDocumentIds,
              utils::CancellationToken cancelToken = {},
              const std::function<void()> &downgradeLock = {}) const override;

  utils::ScopedDisposable
  onUpdate(std::function<void(std::span<const DocumentId> changedDocumentIds,
                              std::span<const DocumentId> deletedDocumentIds)>
               listener) const override;
  utils::ScopedDisposable
  onBuildPhase(DocumentState targetState,
               std::function<void(
                   std::span<const std::shared_ptr<Document>> builtDocuments,
                   utils::CancellationToken cancelToken)>
                   listener) const override;
  utils::ScopedDisposable
  onDocumentPhase(DocumentState targetState,
                  std::function<void(const std::shared_ptr<Document> &,
                                     utils::CancellationToken cancelToken)>
                      listener) const override;

  void waitUntil(DocumentState state,
                 utils::CancellationToken cancelToken = {}) const override;
  [[nodiscard]] DocumentId
  waitUntil(DocumentState state, DocumentId documentId,
            utils::CancellationToken cancelToken = {}) const override;

  void resetToState(Document &document, DocumentState state) const override;

private:
  static constexpr std::size_t kDocumentStateCount =
      static_cast<std::size_t>(DocumentState::Validated) + 1;

  using UpdateListener =
      std::function<void(std::span<const DocumentId> changedDocumentIds,
                         std::span<const DocumentId> deletedDocumentIds)>;
  using BuildPhaseListener = std::function<void(
      std::span<const std::shared_ptr<Document>> builtDocuments,
      utils::CancellationToken cancelToken)>;
  using DocumentPhaseListener =
      std::function<void(const std::shared_ptr<Document> &document,
                         utils::CancellationToken cancelToken)>;

  template <typename Listener> struct ListenerEntry {
    std::size_t id = 0;
    Listener listener;
  };
  template <typename Listener> struct ListenerState {
    mutable std::mutex mutex;
    std::vector<ListenerEntry<Listener>> listeners;
    std::size_t nextId = 0;
  };

  [[nodiscard]] BuildOptions getBuildOptions(const Document &document) const;
  [[nodiscard]] bool shouldLink(const Document &document) const;
  [[nodiscard]] bool shouldValidate(const Document &document) const;
  [[nodiscard]] std::vector<std::string>
  findMissingValidationCategories(const Document &document,
                                  const BuildOptions &options) const;
  [[nodiscard]] bool resultsAreIncomplete(const Document &document,
                                          const BuildOptions &options) const;
  [[nodiscard]] bool
  shouldRelink(const Document &document,
               const std::unordered_set<DocumentId> &changedDocumentIds) const;
  // Drops the cached lazy resolution of references that a lazily-linked (and so
  // un-indexed) document resolved into a document that just changed or was
  // deleted, so a later access re-resolves instead of dereferencing a raw
  // target pointer into freed arena memory.
  void clearStaleLazyLinks(
      Document &document,
      const std::unordered_set<DocumentId> &changedDocumentIds,
      utils::CancellationToken cancelToken) const;
  [[nodiscard]] bool
  hasTextDocument(const std::shared_ptr<Document> &document) const;

  void emitUpdate(std::span<const DocumentId> changedDocumentIds,
                  std::span<const DocumentId> deletedDocumentIds) const;
  [[nodiscard]] std::vector<std::shared_ptr<Document>>
  sortDocuments(std::vector<std::shared_ptr<Document>> documents) const;
  void prepareBuild(std::span<const std::shared_ptr<Document>> documents,
                    const BuildOptions &options) const;
  void notifyBuildPhase(std::span<const std::shared_ptr<Document>> documents,
                        DocumentState targetState,
                        utils::CancellationToken cancelToken) const;
  void notifyDocumentPhase(const std::shared_ptr<Document> &document,
                           DocumentState targetState,
                           utils::CancellationToken cancelToken) const;
  void publishWorkspaceState(DocumentState targetState) const;
  // Records one document's progress by setting its state to @p targetState.
  // Per-document phase listeners are NOT notified here: runMergedPhase fires them
  // serially and in document order once the phase has drained.
  void advance(const std::shared_ptr<Document> &document,
               DocumentState targetState) const;
  // Runs @p body once per document whose state is below @p phaseEnd, in
  // parallel, then publishes each workspace state in @p publishStates (in order)
  // once the whole phase has drained. body advances each document through its
  // sub-states itself (via advance), gated on the document's entry state.
  template <typename Body>
  void runMergedPhase(std::vector<std::shared_ptr<Document>> &documents,
                      DocumentState phaseEnd,
                      std::initializer_list<DocumentState> publishStates,
                      utils::CancellationToken cancelToken, Body &&body) const;
  void buildDocuments(std::span<const std::shared_ptr<Document>> documents,
                      const BuildOptions &options,
                      utils::CancellationToken cancelToken,
                      const std::function<void()> &downgradeLock) const;
  void markAsCompleted(const Document &document) const;
  void validate(Document &document, utils::CancellationToken cancelToken) const;
  void awaitBuilderState(DocumentState state,
                         utils::CancellationToken cancelToken) const;
  [[nodiscard]] DocumentId
  awaitDocumentState(DocumentState state, DocumentId documentId,
                     utils::CancellationToken cancelToken) const;
  void cleanUpDeleted(DocumentId documentId) const;
  template <typename Listener>
  [[nodiscard]] utils::ScopedDisposable
  addListener(const std::shared_ptr<ListenerState<Listener>> &state,
              Listener listener) const;
  template <typename Listener>
  [[nodiscard]] std::vector<ListenerEntry<Listener>>
  snapshotListeners(
      const std::shared_ptr<ListenerState<Listener>> &state) const;
  template <typename Listener>
  [[nodiscard]] static std::array<std::shared_ptr<ListenerState<Listener>>,
                                  kDocumentStateCount>
  makeListenerStates();

  BuildOptions _updateBuildOptions{};

  mutable std::mutex _stateMutex;
  mutable std::condition_variable _stateCv;
  mutable DocumentState _currentState = DocumentState::Changed;
  mutable std::array<std::size_t, kDocumentStateCount>
      _publishedWorkspaceStates{};
  mutable std::unordered_map<DocumentId, DocumentBuildState>
      _buildStateByDocumentId;

  std::shared_ptr<ListenerState<UpdateListener>> _updateListeners =
      std::make_shared<ListenerState<UpdateListener>>();
  std::array<std::shared_ptr<ListenerState<BuildPhaseListener>>,
             kDocumentStateCount>
      _buildPhaseListeners = makeListenerStates<BuildPhaseListener>();
  std::array<std::shared_ptr<ListenerState<DocumentPhaseListener>>,
             kDocumentStateCount>
      _documentPhaseListeners = makeListenerStates<DocumentPhaseListener>();
};

template <typename Body>
void DefaultDocumentBuilder::runMergedPhase(
    std::vector<std::shared_ptr<Document>> &documents, DocumentState phaseEnd,
    std::initializer_list<DocumentState> publishStates,
    utils::CancellationToken cancelToken, Body &&body) const {
  auto bodyFn = std::forward<Body>(body);
  auto *taskScheduler = shared.taskScheduler.get();

  std::vector<std::size_t> pendingIndexes;
  std::vector<DocumentState> entryStates;
  pendingIndexes.reserve(documents.size());
  entryStates.reserve(documents.size());
  for (std::size_t index = 0; index < documents.size(); ++index) {
    if (documents[index]->state < phaseEnd) {
      pendingIndexes.push_back(index);
      entryStates.push_back(documents[index]->state);
    }
  }

  // Run the per-document work in parallel; each document is touched by exactly
  // one task (single writer), so advancing document->state inside body is safe.
  const auto runOne = [&](std::size_t pos) {
    bodyFn(documents[pendingIndexes[pos]], entryStates[pos], cancelToken);
  };

  if (taskScheduler == nullptr || pendingIndexes.size() <= 1) {
    for (std::size_t pos = 0; pos < pendingIndexes.size(); ++pos) {
      utils::throw_if_cancelled(cancelToken);
      runOne(pos);
    }
  } else {
    taskScheduler->parallelFor(
        cancelToken, std::views::iota(std::size_t{0}, pendingIndexes.size()),
        [&runOne](std::size_t pos) { runOne(pos); });
  }

  // Notify per-document phase listeners serially and in document order (which
  // prioritizes open documents), for every state each document newly reached
  // this phase. Doing this after the barrier keeps listeners single-threaded and
  // the notification order deterministic.
  for (std::size_t pos = 0; pos < pendingIndexes.size(); ++pos) {
    const auto &document = documents[pendingIndexes[pos]];
    for (const auto state : publishStates) {
      if (entryStates[pos] < state && state <= document->state) {
        notifyDocumentPhase(document, state, cancelToken);
      }
    }
  }

  // Publish the workspace-level milestones once the phase has drained: each
  // state still advances _currentState (so awaitBuilderState keeps working),
  // batched into one burst per phase instead of one per state transition.
  for (const auto state : publishStates) {
    std::vector<std::shared_ptr<Document>> reached;
    reached.reserve(documents.size());
    for (const auto &document : documents) {
      if (document->state >= state) {
        reached.push_back(document);
      }
    }
    notifyBuildPhase(reached, state, cancelToken);
    publishWorkspaceState(state);
  }
}

template <typename Listener>
utils::ScopedDisposable DefaultDocumentBuilder::addListener(
    const std::shared_ptr<ListenerState<Listener>> &state,
    Listener listener) const {
  std::size_t id = 0;
  {
    std::scoped_lock lock(state->mutex);
    id = state->nextId++;
    state->listeners.push_back({.id = id, .listener = std::move(listener)});
  }

  return utils::ScopedDisposable([state, id]() {
    std::scoped_lock lock(state->mutex);
    std::erase_if(state->listeners,
                  [id](const auto &entry) { return entry.id == id; });
  });
}

template <typename Listener>
std::vector<DefaultDocumentBuilder::ListenerEntry<Listener>>
DefaultDocumentBuilder::snapshotListeners(
    const std::shared_ptr<ListenerState<Listener>> &state) const {
  std::scoped_lock lock(state->mutex);
  return state->listeners;
}

template <typename Listener>
auto DefaultDocumentBuilder::makeListenerStates()
    -> std::array<std::shared_ptr<ListenerState<Listener>>,
                  kDocumentStateCount> {
  std::array<std::shared_ptr<ListenerState<Listener>>, kDocumentStateCount>
      states;
  for (auto &state : states) {
    state = std::make_shared<ListenerState<Listener>>();
  }
  return states;
}

} // namespace pegium::workspace
