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
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/BuildOptions.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"

namespace cursiv {

struct CoreServices;
struct SharedCoreServices;

// Documents advance in parallel, with barriers before scoping and linking.
// New exports may invalidate existing links; those resets precede publication
// and any reader admitted by the workspace lock. Every service is read through
// `_shared` when used, so one replaced between two builds dangles nothing.
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
  // The phase's work list, taken before the fan-out so the parallel bodies
  // share no filtering. One predicate, not three: a hand-written predicate
  // per rung (one for linking, one for relinking, one for validating) would
  // mean a hand-written loop per rung; here a phase is rung-generic, so the
  // rung is a parameter and the three collapse into this.
  [[nodiscard]] virtual std::vector<Document*> pending(
      std::span<Document* const> all, DocumentState phaseEnd) const;

  virtual void validate(Document&, const CoreServices&,
                        std::span<const ValidationCategory* const> categories,
                        const CancellationToken&) const;

  // What this document was last built with, or `updateBuildOptions` when it has
  // never been built. A copy: the table it comes from is written under
  // `_stateMutex`, and a phase body reads this off the lock.
  [[nodiscard]] virtual BuildOptions getBuildOptions(const Document&) const;

  // Requests are sealed between phases, before workers read their options.
  virtual void prepareBuild(std::span<Document* const>,
                            const BuildOptions&) const;

  // The phases themselves, over documents that already carry their request.
  // Split from `build` because `update` rebuilds documents it was never
  // asked about, and seeding them with the caller's options would erase
  // what each was actually built with.
  // Content preparation may reset additional dependants. It must finish before
  // settle admits readers; every subsequent phase only advances documents, so
  // an override that resets one after calling the base frees what a reader
  // may already hold.
  virtual void buildDocuments(std::span<Document* const>,
                              const CancellationToken&) const;

  // What `requested` still owes this document: the request expanded -- empty
  // being the built-in half plus every category the language's registry
  // declares -- minus what already ran on this revision.
  [[nodiscard]] virtual std::vector<const ValidationCategory*>
  findMissingValidationCategories(
      const Document&, const CoreServices&,
      std::span<const ValidationCategory* const> requested) const;

  // A rung reached, told to whoever subscribed. Called on the publishing thread
  // once the phase has drained, with the documents that crossed the rung in
  // this build.
  virtual void notifyBuildPhase(std::span<Document* const>, DocumentState,
                                const CancellationToken&) const;

  // Does this document still owe what was asked of it? A `Validated` document
  // is done with the categories of its request, never with everything -- so a
  // wider request after a narrower one reopens it here and nowhere else.
  [[nodiscard]] virtual bool resultsAreIncomplete(const Document&) const;

  // A document leaving the workspace: its derived halves and its request go
  // with it, then the document itself.
  virtual void cleanUpDeleted(std::string_view uri) const;

  // The categories that just ran, recorded against the revision they ran on --
  // which is what makes `Validated` mean "validated for what was asked so far".
  virtual void markAsCompleted(
      const Document&, std::span<const ValidationCategory* const> ran) const;

  // What one reported change really touched. A URI naming a document held is
  // itself; one naming a directory is every document under it -- which is how a
  // checkout, a bulk copy or a watcher that coalesced its events lands. A URI
  // that is neither touches nothing.
  [[nodiscard]] virtual std::vector<std::string> findChangedUris(
      std::string_view changed) const;

  // What the workspace lost or gained, told before the rebuild that answers it.
  virtual void emitUpdate(std::span<const std::string_view> changed,
                          std::span<const std::string_view> deleted) const;

  // One document's rung, told from the worker that carried it. Every listener
  // hears it: one that throws `OperationCancelled` does not silence the next.
  virtual void notifyDocumentPhase(Document&, DocumentState,
                                   const CancellationToken&) const;

 private:
  // The language of one document, or null when nothing claims its URI. A
  // document nothing claims is skipped rather than failed: it keeps its rung,
  // and a wait on it answers `Unreachable`.
  [[nodiscard]] const CoreServices* languageOf(const Document&) const noexcept;

  // One rung of a phase and the work that reaches it.
  struct Step {
    DocumentState rung;
    cursiv::function_ref<void(Document&, const CoreServices&,
                              const CancellationToken&)>
        body;
  };

  // A document runs the steps above the rung it entered at, and the rungs are
  // published once the phase has drained, to the documents that crossed them.
  // Each step is announced as it is crossed -- or, with `beforePublish`, after
  // it has run, even when the phase failed.
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
  [[nodiscard]] std::uint64_t beginAttempt() const;
  // A document's rung is heard by whoever waits on that document, not only at
  // the end of the phase.
  void wakeDocumentWaiters() const;
  void publishWorkspaceState(DocumentState) const;
  void publishFailure(std::uint64_t attempt) const;
  [[nodiscard]] bool failedSince(std::uint64_t attempt) const noexcept {
    return _failedAttempt != 0 && _failedAttempt >= attempt;
  }

  SharedCoreServices& _shared;
  BuildOptions _updateBuildOptions;
  // Read inside builds without a lock, under the registration law of the
  // interface: listeners are configuration, installed outside any build.
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
  // Raised under `_stateMutex` by a per-document wait: with none, a crossed
  // rung costs one load.
  mutable std::atomic<std::uint32_t> _documentWaiters = 0;
  // How many times each rung was published: a waiter woken by the publication
  // it awaits has been served, even if a later build already dropped
  // `_currentState` back.
  mutable std::array<std::uint64_t, kDocumentStateCount> _published{};
  // The categories that ran per document on its current revision, which is what
  // makes `Validated` mean "validated for what was asked so far". Guarded by
  // `_stateMutex`, the validating phase recording from its parallel bodies.
  //
  // Two invalidations, because there are two reset paths: `resetToState` erases
  // the entry with the diagnostics it explains, while the factory's
  // `attachTextDocument` drops a tree without the builder -- so the record also
  // carries the revision it was made on, and a stale one counts as nothing ran.
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
