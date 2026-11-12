#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"

namespace cursiv {

struct CoreServices;
struct SharedCoreServices;

// Documents advance in parallel, with barriers before scoping and linking.
// Resets for new exports happen before any reader is let in. Services are
// read through `_shared` at each use, so one replaced between builds is safe.
class DefaultDocumentBuilder : public DocumentBuilder {
 public:
  explicit DefaultDocumentBuilder(SharedCoreServices& shared);

  void build(std::span<Document* const>, const BuildOptions& = {},
             const CancellationToken& = {}) const override;
  void update(std::span<const std::string_view> changed,
              std::span<const std::string_view> deleted,
              const CancellationToken& = {}) const override;
  void resetToState(Document&, DocumentState) const override;
  [[nodiscard]] BuildOptions& updateBuildOptions() noexcept override {
    return _updateBuildOptions;
  }
  [[nodiscard]] const BuildOptions& updateBuildOptions()
      const noexcept override {
    return _updateBuildOptions;
  }

  void addBuildPhaseListener(BuildPhaseListener&) override;
  void removeBuildPhaseListener(BuildPhaseListener&) override;
  void addDocumentPhaseListener(DocumentPhaseListener&) override;
  void removeDocumentPhaseListener(DocumentPhaseListener&) override;
  void addDocumentUpdateListener(DocumentUpdateListener&) override;
  void removeDocumentUpdateListener(DocumentUpdateListener&) override;

  [[nodiscard]] WaitOutcome waitUntil(
      DocumentState, const CancellationToken& = {}) const override;
  [[nodiscard]] WaitOutcome waitUntil(
      DocumentState, DocumentId, const CancellationToken& = {}) const override;
  [[nodiscard]] DocumentState currentState() const noexcept override;
  ~DefaultDocumentBuilder() noexcept override = default;

 protected:
  // The phase's work list, taken before the parallel bodies run. `all` holds
  // each document once, and an override must keep it so: two bodies on one
  // document would run at the same time.
  [[nodiscard]] virtual std::vector<Document*> pending(
      std::span<Document* const> all, DocumentState phaseEnd) const;

  virtual void validate(Document&, const CoreServices&,
                        std::span<const ValidationCategory* const> categories,
                        const CancellationToken&) const;

  // What this document was last built with, or `updateBuildOptions` when it has
  // never been built. A copy: the table it comes from is written under
  // `_stateMutex`, and a phase body reads this off the lock.
  [[nodiscard]] virtual BuildOptions getBuildOptions(const Document&) const;

  // Requests are fixed between phases, before workers read their options.
  virtual void prepareBuild(std::span<Document* const>,
                            const BuildOptions&) const;

  // The phases, over documents that already carry their request; `update`
  // rebuilds documents it was not asked about, with their own options. Every
  // reset precedes `allowImmediateReads`: later phases only advance documents.
  virtual void buildDocuments(std::span<Document* const>,
                              const CancellationToken&) const;

  // The categories of `requested` this document still needs: the request
  // expanded (empty is the built-in half plus every category the language's
  // registry declares), minus what already ran on this revision.
  [[nodiscard]] virtual std::vector<const ValidationCategory*>
  findMissingValidationCategories(
      const Document&, const CoreServices&,
      std::span<const ValidationCategory* const> requested) const;

  // A state reached, told to the listeners. Called on the publishing thread
  // once the phase has drained, with the documents that reached the state in
  // this build.
  virtual void notifyBuildPhase(std::span<Document* const>, DocumentState,
                                const CancellationToken&) const;

  // Whether this document still lacks part of its request. A `Validated`
  // document is done with the categories of its request, never with
  // everything, so a wider request after a narrower one reopens it here.
  [[nodiscard]] virtual bool resultsAreIncomplete(const Document&) const;

  // A document leaving the workspace: its derived halves and its request go
  // with it, then the document itself.
  virtual void cleanUpDeleted(std::string_view uri) const;

  // Records the categories that just ran against the document's revision.
  // Called before their diagnostics are published: an override records all of
  // `ran` or none of it.
  virtual void markAsCompleted(
      const Document&, std::span<const ValidationCategory* const> ran) const;

  // The URIs one reported change touches: a held document is itself, a
  // directory is every document under it, a file the store lacks is that file.
  // A URI `WorkspaceManager::isIncluded` refuses touches nothing.
  [[nodiscard]] virtual std::vector<std::string> findChangedUris(
      std::string_view changed) const;

  // What the workspace lost or gained, told before the rebuild that answers it.
  virtual void emitUpdate(std::span<const std::string_view> changed,
                          std::span<const std::string_view> deleted) const;

  // Tells one document's state to every listener, in registration order, from
  // the worker that reached it. `OperationCancelled` counts as received;
  // another failure is rethrown after the last listener.
  virtual void notifyDocumentPhase(Document&, DocumentState,
                                   const CancellationToken&) const;

 private:
  // Documents that reference the moved ones go back to `ComputedScopes`, to
  // link again. `repair` also takes a document with a broken reference.
  void letGoOf(const std::unordered_set<DocumentId>& moved, bool repair) const;
  // Resets one document's state, and not the documents that read it.
  void resetDocument(Document& document, DocumentState state) const;

  // The language of one document, or null when nothing claims its URI. A
  // document nothing claims is skipped rather than failed: it keeps its state,
  // and a wait on it answers `Unreachable`.
  [[nodiscard]] const CoreServices* languageOf(const Document&) const noexcept;

  // One state of a phase and the work that reaches it.
  struct Step {
    DocumentState state;
    cursiv::function_ref<void(Document&, const CoreServices&,
                              const CancellationToken&)>
        body;
  };

  // A document runs the steps above the state it entered at, and each state is
  // published once the phase has drained. A step is told as it is reached, or,
  // with `beforePublish`, after it runs, even when the phase failed.
  void runPhase(std::span<Document* const> all,
                std::initializer_list<Step> steps,
                const CancellationToken&,
                std::optional<cursiv::function_ref<void()>> beforePublish =
                    std::nullopt) const;

  static void computeLocalScopes(Document&, const CoreServices&,
                                 const CancellationToken&);
  static void bindReferences(Document&, const CoreServices&);
  // A request some build started and none finished.
  [[nodiscard]] bool requestUnfinished(const Document&) const;
  // What `update` does before the phases: the store and the derived halves
  // brought in line with the change, the listeners told. Returns the documents
  // to build.
  [[nodiscard]] std::vector<Document*> applyUpdate(
      std::span<const std::string_view> changed,
      std::span<const std::string_view> deleted,
      const CancellationToken&) const;
  void runAttempt(cursiv::function_ref<void()> body) const;
  [[nodiscard]] std::uint64_t beginAttempt() const;
  // A document's new state wakes whoever waits on that document, not only at
  // the end of the phase.
  void wakeDocumentWaiters(const Document&) const;
  void publishWorkspaceState(DocumentState) const;
  void publishFailure(std::uint64_t attempt) const;
  [[nodiscard]] bool failedSince(std::uint64_t attempt) const noexcept {
    return _failedAttempt != 0 && _failedAttempt >= attempt;
  }

  SharedCoreServices& _shared;
  BuildOptions _updateBuildOptions;
  // Read inside builds without a lock, under the interface's registration
  // rule: listeners are configuration, installed outside any build.
  std::vector<BuildPhaseListener*> _phaseListeners;
  std::vector<DocumentPhaseListener*> _documentPhaseListeners;
  std::vector<DocumentUpdateListener*> _updateListeners;

  mutable std::mutex _stateMutex;
  // `condition_variable_any`: its `stop_token` overload registers the callback
  // and the wait as one operation, so a cancellation racing the last predicate
  // test cannot be lost.
  mutable std::condition_variable_any _stateCv;
  // Written under `_stateMutex`; atomic so `currentState` reads it off the
  // lock.
  mutable std::atomic<DocumentState> _currentState = DocumentState::Changed;
  mutable std::uint64_t _buildAttempt = 0;
  mutable std::uint64_t _failedAttempt = 0;  // 0 is "none"
  // Raised under `_stateMutex` by a per-document wait: with none, a state
  // change costs one load.
  mutable std::atomic<std::uint32_t> _documentWaiters = 0;
  // The document of each such wait, under `_stateMutex`: a document nobody
  // waits on wakes nobody.
  mutable std::vector<DocumentId> _awaited;
  // How many times each state was published: a waiter woken by the publication
  // it awaits has been served, even if a later build already dropped
  // `_currentState` back.
  mutable std::array<std::uint64_t, kDocumentStateCount> _published{};
  // The categories that ran per document, with the revision they ran on: a
  // record of an older revision counts as nothing ran. Under `_stateMutex`.
  struct ValidatedRecord {
    std::uint64_t revision = 0;
    std::vector<const ValidationCategory*> ran;
  };
  mutable std::unordered_map<DocumentId, ValidatedRecord> _validated;
  // A document keeps what it was asked until the request is honoured or the
  // document reset: an interrupted build leaves `completed` false, and the
  // next `prepareBuild` keeps the original ask instead of overwriting it.
  struct BuildRequest {
    BuildOptions options;
    bool completed = false;
  };
  // Guarded by `_stateMutex`, written by `prepareBuild`, the end of a finished
  // build, and `resetToState`.
  mutable std::unordered_map<DocumentId, BuildRequest> _buildOptions;
};

}  // namespace cursiv
