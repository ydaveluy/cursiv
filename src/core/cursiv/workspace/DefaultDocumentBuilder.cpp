#include "cursiv/workspace/DefaultDocumentBuilder.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/execution/TaskScheduler.hpp"
#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/references/Linker.hpp"
#include "cursiv/references/ScopeComputation.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/Diagnostic.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/validation/DocumentValidator.hpp"
#include "cursiv/validation/ValidationCategory.hpp"
#include "cursiv/validation/ValidationRegistry.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/workspace/IndexManager.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"
#include "cursiv/workspace/WorkspaceManager.hpp"

namespace cursiv {

DefaultDocumentBuilder::DefaultDocumentBuilder(SharedCoreServices& shared)
    : _shared(shared), _updateBuildOptions{true, {&built_in, &fast}} {}

const CoreServices* DefaultDocumentBuilder::languageOf(
    const Document& document) const noexcept {
  return document.language;
}

std::vector<Document*> DefaultDocumentBuilder::pending(
    std::span<Document* const> all, DocumentState phaseEnd) const {
  std::vector<Document*> work;
  work.reserve(all.size());
  for (Document* d : all)
    if (d->state.load() < phaseEnd && languageOf(*d) != nullptr &&
        (phaseEnd != DocumentState::Validated ||
         getBuildOptions(*d).validation))
      work.push_back(d);
  // The open ones first, stably: the documents someone is looking at reach
  // every rung, and so the client, before the rest of the store.
  std::ranges::stable_partition(work,
                                [](const Document* d) { return d->isOpen; });
  return work;
}

void DefaultDocumentBuilder::runPhase(std::span<Document* const> all,
                                      std::initializer_list<Step> steps,
                                      const CancellationToken& token,
                                      std::optional<cursiv::function_ref<void()>>
                                          beforePublish) const {
  const DocumentState phaseEnd = (steps.end() - 1)->rung;
  const std::vector<Document*> work = pending(all, phaseEnd);
  // Tested even when there is nothing to do: a workspace whose whole tail is
  // unclaimed would otherwise never look at the token again.
  throw_if_cancelled(token);
  // Where each document entered: a rung is announced with the documents of
  // this phase that stand on it once the phase is over.
  std::vector<DocumentState> entries(work.size());
  for (std::size_t i = 0; i < work.size(); ++i)
    entries[i] = work[i]->state.load();
  const auto run = [&](const Step& step, std::size_t i) {
    if (entries[i] >= step.rung) return;
    throw_if_cancelled(token);
    Document& d = *work[i];
    step.body(d, *languageOf(d), token);
    d.state = step.rung;
    wakeDocumentWaiters(d);
    if (!beforePublish) notifyDocumentPhase(d, step.rung, token);
  };
  const auto indices = std::views::iota(std::size_t{0}, work.size());
  std::exception_ptr failure;
  try {
    // The pool is the workspace's, a required slot: no null check, as the slot
    // law demands -- a container that owes it is refused at registration.
    _shared.taskScheduler->parallelFor(token, indices, [&](std::size_t i) {
      for (const Step& step : steps) run(step, i);
    });
  } catch (...) {
    if (!beforePublish) throw;
    failure = std::current_exception();
  }
  if (beforePublish) {
    // Published exports can invalidate old links even when this phase failed.
    (*beforePublish)();
    if (!_documentPhaseListeners.empty()) {
      std::mutex failures;
      _shared.taskScheduler->parallelFor(kNoCancel, indices, [&](std::size_t i) {
        for (const Step& step : steps)
          if (entries[i] < step.rung && work[i]->state.load() >= step.rung) {
            try {
              notifyDocumentPhase(*work[i], step.rung, token);
            } catch (...) {
              const std::scoped_lock lock(failures);
              if (failure == nullptr) failure = std::current_exception();
            }
          }
      });
    }
    if (failure != nullptr) std::rethrow_exception(failure);
  }
  throw_if_cancelled(token);
  std::vector<Document*> reached;
  reached.reserve(work.size());
  for (const Step& step : steps) {
    reached.clear();
    for (std::size_t i = 0; i < work.size(); ++i)
      if (entries[i] <= step.rung) reached.push_back(work[i]);
    publishWorkspaceState(step.rung);
    // After the rung is published, on this thread: a listener that waits on the
    // rung it is being told about must not deadlock, and a cancelled phase --
    // gone by exception above -- is never announced.
    notifyBuildPhase(reached, step.rung, token);
  }
}

void DefaultDocumentBuilder::notifyBuildPhase(
    std::span<Document* const> documents, DocumentState state,
    const CancellationToken& token) const {
  for (BuildPhaseListener* listener : _phaseListeners)
    listener->onBuildPhase(state, documents, token);
}

bool DefaultDocumentBuilder::resultsAreIncomplete(
    const Document& document) const {
  const CoreServices* const language = languageOf(document);
  if (language == nullptr) return false;
  const BuildOptions owed = getBuildOptions(document);
  return owed.validation && !findMissingValidationCategories(
                                 document, *language, owed.validationCategories)
                                 .empty();
}

void DefaultDocumentBuilder::cleanUpDeleted(std::string_view uri) const {
  Documents& store = *_shared.documents;
  if (const std::shared_ptr<Document> d = store.getDocument(uri))
    resetDocument(*d, DocumentState::Changed);
  (void)store.deleteDocument(uri);
}

void DefaultDocumentBuilder::markAsCompleted(
    const Document& document,
    std::span<const ValidationCategory* const> ran) const {
  const std::scoped_lock lock(_stateMutex);
  ValidatedRecord& record = _validated[document.id];
  if (record.revision != document.buildRevision) {
    record.revision = document.buildRevision;
    record.ran.clear();
  }
  // All or none: past the reservation nothing allocates, so a refusal records
  // no category of `ran`.
  record.ran.reserve(record.ran.size() + ran.size());
  for (const ValidationCategory* category : ran)
    if (std::ranges::find(record.ran, category) == record.ran.end())
      record.ran.push_back(category);
}

std::vector<std::string> DefaultDocumentBuilder::findChangedUris(
    std::string_view changed) const {
  // The common case, and the cheap one: the workspace already holds it.
  if (_shared.documents->hasDocument(changed)) return {std::string(changed)};

  const FileSystemProvider& files = *_shared.fileSystemProvider;
  const WorkspaceManager& workspace = *_shared.workspaceManager;
  try {
    const FileSystemNode entry = files.stat(changed);
    // The scan's own filter, the folders above the entry included: an event
    // inside a folder the scan leaves out brings nothing in.
    if (!workspace.isIncluded(entry)) return {};
    if (entry.isDirectory) return workspace.searchFolder(changed);
    return {std::string(changed)};
  } catch (const FileSystemError&) {
    // Nothing readable there: a deletion reported as a change, a race with the
    // editor. It touched nothing this workspace can name.
  }
  return {};
}

void DefaultDocumentBuilder::emitUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted) const {
  for (DocumentUpdateListener* const listener : _updateListeners)
    listener->onUpdate(changed, deleted);
}

void DefaultDocumentBuilder::notifyDocumentPhase(
    Document& document, DocumentState state,
    const CancellationToken& token) const {
  for (DocumentPhaseListener* listener : _documentPhaseListeners) {
    try {
      listener->onDocumentPhase(document, state, token);
    } catch (const OperationCancelled&) {
      // The phase tests the token itself, once every listener has heard.
    }
  }
}

void DefaultDocumentBuilder::addDocumentPhaseListener(
    DocumentPhaseListener& listener) {
  _documentPhaseListeners.push_back(&listener);
}

void DefaultDocumentBuilder::removeDocumentPhaseListener(
    DocumentPhaseListener& listener) {
  std::erase(_documentPhaseListeners, &listener);
}

void DefaultDocumentBuilder::addBuildPhaseListener(
    BuildPhaseListener& listener) {
  _phaseListeners.push_back(&listener);
}

void DefaultDocumentBuilder::removeBuildPhaseListener(
    BuildPhaseListener& listener) {
  std::erase(_phaseListeners, &listener);
}

void DefaultDocumentBuilder::addDocumentUpdateListener(
    DocumentUpdateListener& listener) {
  _updateListeners.push_back(&listener);
}

void DefaultDocumentBuilder::removeDocumentUpdateListener(
    DocumentUpdateListener& listener) {
  std::erase(_updateListeners, &listener);
}

BuildOptions DefaultDocumentBuilder::getBuildOptions(
    const Document& document) const {
  const std::scoped_lock lock(_stateMutex);
  const auto at = _buildOptions.find(document.id);
  return at == _buildOptions.end() ? _updateBuildOptions : at->second.options;
}

void DefaultDocumentBuilder::prepareBuild(std::span<Document* const> documents,
                                          const BuildOptions& options) const {
  const std::scoped_lock lock(_stateMutex);
  for (const Document* const d : documents) {
    // An unfinished request survives: a document keeps what it was asked
    // until a build honours it, so an interrupted ask is never replaced.
    if (const auto at = _buildOptions.find(d->id);
        at != _buildOptions.end() && !at->second.completed)
      continue;
    _buildOptions[d->id] = {options, false};
  }
}

void DefaultDocumentBuilder::build(std::span<Document* const> documents,
                                   const BuildOptions& options,
                                   const CancellationToken& token) const {
  runAttempt([&] {
    prepareBuild(documents, options);
    std::vector<std::string_view> uris;
    uris.reserve(documents.size());
    for (const Document* const d : documents) uris.push_back(d->uri);

    // A subscriber may send a document back down -- one whose results read
    // what this build changes, through no reference the index sees. This
    // build answers it too, as `update` does.
    const std::vector<std::shared_ptr<Document>> held =
        _shared.documents->all();
    std::vector<DocumentState> before;
    before.reserve(held.size());
    for (const std::shared_ptr<Document>& d : held)
      before.push_back(d->state.load());
    emitUpdate(uris, {});
    std::vector<Document*> work(documents.begin(), documents.end());
    std::unordered_set<const Document*> requested;
    for (std::size_t at = 0; at < held.size(); ++at) {
      Document* const d = held[at].get();
      if (d->state.load() >= before[at]) continue;
      if (requested.empty())
        requested.insert(documents.begin(), documents.end());
      if (requested.insert(d).second) work.push_back(d);
    }
    buildDocuments(work, token);
  });
}

void DefaultDocumentBuilder::buildDocuments(
    std::span<Document* const> documents,
    const CancellationToken& token) const {
  IndexManager& index = *_shared.indexManager;
  const DocumentFactory& factory = *_shared.documentFactory;

  const auto held = _shared.documents->all();
  // Each document once: the phases run their documents in parallel, and a
  // caller's duplicate would be two bodies writing one document's state.
  std::vector<Document*> work;
  std::unordered_set<Document*> scheduled;
  const auto schedule = [&](Document* d) {
    if (scheduled.insert(d).second) work.push_back(d);
  };
  for (Document* const d : documents) schedule(d);
  const auto rebuild = [&](Document* d) {
    if (d->state.load() >= DocumentState::IndexedContent) {
      std::optional<BuildOptions> requested;
      {
        const std::scoped_lock lock(_stateMutex);
        if (const auto at = _buildOptions.find(d->id);
            at != _buildOptions.end() && !at->second.completed)
          requested = at->second.options;
      }
      resetToState(*d, DocumentState::ComputedScopes);
      if (requested) prepareBuild({&d, 1}, *requested);
    }
    schedule(d);
  };
  const std::unordered_set<DocumentId> none;
  for (const auto& heldDocument : held) {
    auto* const d = heldDocument.get();
    if (d->state.load() >= DocumentState::IndexedContent &&
        index.isAffected(*d, none))
      rebuild(d);
  }
  std::unordered_set<DocumentId> changed;
  for (const Document* d : work)
    if (d->state.load() < DocumentState::IndexedContent) changed.insert(d->id);
  const bool prepareContent = !changed.empty() && std::ranges::any_of(
      held, [&](const auto& d) {
        return !changed.contains(d->id) &&
               d->state.load() >= DocumentState::ComputedScopes;
      });

  for (Document* d : work) {
    // A document `Validated` by a narrower request re-enters the validating
    // phase alone: the tree, the scopes and both index halves stand.
    if (d->state.load() == DocumentState::Validated && resultsAreIncomplete(*d))
      d->state = DocumentState::IndexedReferences;
    // Below `IndexedReferences` a memoised resolution has no index row to
    // protect it from the next reset: dropped here, while the write still
    // excludes every reader.
    if (d->state.load() < DocumentState::IndexedReferences)
      if (const CoreServices* language = languageOf(*d); language != nullptr) {
        language->linker->unlink(*d);
        if (d->state.load() == DocumentState::Linked)
          d->state = DocumentState::ComputedScopes;
        // A resumed document may retain its scopes and skip that phase.
        if (d->state.load() >= DocumentState::ComputedScopes)
          bindReferences(*d, *language);
      }
  }

  if (!prepareContent) _shared.workspaceLock->settle();

  const auto publishContent = [&] {
    for (const auto& heldDocument : held) {
      auto* const d = heldDocument.get();
      if (changed.contains(d->id)) continue;
      if (d->state.load() <= DocumentState::ComputedScopes &&
          scheduled.contains(d))
        continue;
      if (index.isAffected(*d, changed)) rebuild(d);
    }
    _shared.workspaceLock->settle();
  };

  runPhase(work,
           {{DocumentState::Parsed,
             [&](Document& d, const CoreServices&,
                 const CancellationToken& phase) { factory.update(d, phase); }},
            {DocumentState::IndexedContent,
             [&](const Document& d, const CoreServices&,
                 const CancellationToken& phase) {
               index.updateContent(d, phase);
             }}},
           token,
           prepareContent
               ? std::optional<cursiv::function_ref<void()>>{publishContent}
               : std::nullopt);

  try {
    // A barrier, not a stage of the linking phase: a derived scope provider
    // may hunt cross-document, and a hunt reading a document another worker
    // was still scoping would memoise a false "not found", differently on
    // each run. Every local scope is written before the first link reads one.
    runPhase(work,
             {{DocumentState::ComputedScopes,
               [](Document& d, const CoreServices& language,
                  const CancellationToken& phase) {
                 computeLocalScopes(d, language, phase);
               }}},
             token);

    runPhase(work,
             {{DocumentState::Linked,
               [](Document& d, const CoreServices& language,
                  const CancellationToken& phase) {
                 language.linker->link(d, phase);
               }},
              {DocumentState::IndexedReferences,
               [&](const Document& d, const CoreServices&,
                   const CancellationToken& phase) {
                 index.updateReferences(d, phase);
               }}},
             token);
  } catch (...) {
    // Linked but not indexed is a rung the document cannot keep: back to
    // `ComputedScopes`, so the next build relinks it. Its memo is dropped
    // there, before that build settles -- nothing is touched past this one's.
    for (Document* d : work)
      if (d->state.load() == DocumentState::Linked)
        d->state = DocumentState::ComputedScopes;
    throw;
  }

  runPhase(work,
           {{DocumentState::Validated,
             [this](Document& d, const CoreServices& language,
                    const CancellationToken& phase) {
               // Per document, not per build: each keeps what it was asked.
               const BuildOptions owed = getBuildOptions(d);
               validate(d, language, owed.validationCategories, phase);
             }}},
           token);

  const std::scoped_lock lock(_stateMutex);
  for (const Document* const d : work)
    if (const auto at = _buildOptions.find(d->id); at != _buildOptions.end()) {
      const DocumentState last = at->second.options.validation
                                     ? DocumentState::Validated
                                     : DocumentState::IndexedReferences;
      if (d->state.load() >= last)
        at->second.completed = true;
    }
}

void DefaultDocumentBuilder::update(std::span<const std::string_view> changed,
                                    std::span<const std::string_view> deleted,
                                    const CancellationToken& token) const {
  runAttempt(
      [&] { buildDocuments(applyUpdate(changed, deleted, token), token); });
}

std::vector<Document*> DefaultDocumentBuilder::applyUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted,
    const CancellationToken& token) const {
  Documents& store = *_shared.documents;

  // Expanded first, and everything below reads the expansion: a folder reported
  // as changed is every document under it, and a subscriber told the folder
  // would have nothing to look up.
  std::vector<std::string> expanded;
  for (const std::string_view uri : changed)
    for (std::string& found : findChangedUris(uri))
      expanded.push_back(std::move(found));
  std::ranges::sort(expanded);
  expanded.erase(std::ranges::unique(expanded).begin(), expanded.end());
  const std::vector<std::string_view> changedUris(expanded.begin(),
                                                  expanded.end());

  // A deleted folder is every document under it, the same way.
  std::vector<std::string> gone;
  for (const std::string_view uri : deleted) {
    if (store.hasDocument(uri)) {
      gone.emplace_back(uri);
      continue;
    }
    for (const std::shared_ptr<Document>& d : store.getDocuments(uri))
      gone.push_back(d->uri);
  }
  std::ranges::sort(gone);
  gone.erase(std::ranges::unique(gone).begin(), gone.end());
  const std::vector<std::string_view> deletedUris(gone.begin(), gone.end());

  // The moved set, as identities: a URI keeps its id for ever, so a document
  // that has already been destroyed is still nameable here.
  std::unordered_set<DocumentId> moved;
  const auto note = [&](std::string_view uri) {
    if (const DocumentId id = store.getDocumentId(uri); id != kNoDocument)
      moved.insert(id);
  };
  for (const std::string_view uri : changedUris) note(uri);
  for (const std::string_view uri : deletedUris) note(uri);

  // Cause (a) before any look at the moved set, and it plays on an empty one:
  // this is the whole repair path for a reference broken by a symbol that has
  // since appeared somewhere else.
  letGoOf(moved, true);

  // Then the moved ones, all the way down, and before the text is replaced or
  // the document destroyed: the index outlives the arena it describes.
  // An open document standing at `Parsed` holds the parse of its own buffer,
  // paid for by the opening: reset, it would pay it again.
  for (const std::string_view uri : changedUris)
    if (const std::shared_ptr<Document> d = store.getDocument(uri))
      if (!(d->isOpen && d->state == DocumentState::Parsed))
        resetDocument(*d, DocumentState::Changed);
  for (const std::string_view uri : deletedUris) cleanUpDeleted(uri);

  // The request lands on what moved. A document cause (a) reset lost its own
  // with its diagnostics and builds under the update's; one left standing
  // keeps its own.
  //
  // A changed URI the store does not hold is installed here: a file created
  // outside the editor reaches the workspace by no other door, and
  // `findChangedUris` has already dropped what no language claims. Deletion
  // followed by change recreates the same URI.
  //
  // Installed empty at `Changed`: the parse phase reads it and parses it, in
  // parallel and under the token.
  const DocumentFactory& factory = *_shared.documentFactory;
  std::vector<Document*> requested;
  for (const std::string_view uri : changedUris) {
    if (!store.hasDocument(uri)) {
      const std::string canonical = normalize_uri(uri);
      if (const CoreServices* const services =
              _shared.serviceRegistry->findServices(canonical)) {
        const std::shared_ptr<Document> created = factory.createDocument(
            TextDocument::create(canonical,
                                 services->languageMetaData.languageId, 0, {}),
            canonical);
        created->language = services;
        store.addDocument(created);
      }
    }
    if (const std::shared_ptr<Document> d = store.getDocument(uri))
      requested.push_back(d.get());
  }
  prepareBuild(requested, _updateBuildOptions);

  // Announced once the store says it: a subscriber told of a deletion finds
  // the document gone unless it was recreated by the same update, and every
  // changed document stands at `Changed`.
  emitUpdate(changedUris, deletedUris);

  // The one interruption, after every state change: cut short above, a
  // superseded write would leave documents reset and others not.
  throw_if_cancelled(token);

  // What is owed: a rung below the top, a request no build finished, a
  // category the request still owes. A document whose request stops at
  // `IndexedReferences` passes through with nothing left to run.
  std::vector<Document*> work;
  for (const std::shared_ptr<Document>& doc : store.all()) {
    Document* const d = doc.get();
    if (d->state.load() < DocumentState::Validated || requestUnfinished(*d) ||
        resultsAreIncomplete(*d))
      work.push_back(d);
  }
  return work;
}

bool DefaultDocumentBuilder::requestUnfinished(const Document& document) const {
  const std::scoped_lock lock(_stateMutex);
  const auto at = _buildOptions.find(document.id);
  return at != _buildOptions.end() && !at->second.completed;
}

void DefaultDocumentBuilder::letGoOf(
    const std::unordered_set<DocumentId>& moved, bool repair) const {
  const IndexManager& index = *_shared.indexManager;
  for (const std::shared_ptr<Document>& doc : _shared.documents->all()) {
    Document* const d = doc.get();
    if (moved.contains(d->id)) continue;
    if ((repair && d->hasLinkingErrors()) || index.isAffected(*d, moved))
      resetDocument(*d, DocumentState::ComputedScopes);
    else if (!moved.empty() &&
             d->state.load() < DocumentState::IndexedReferences) {
      // Unindexed resolutions have no dependency row to protect their targets.
      if (const CoreServices* language = languageOf(*d); language != nullptr) {
        language->linker->unlink(*d);
        if (d->state.load() == DocumentState::Linked)
          d->state = DocumentState::ComputedScopes;
        if (d->state.load() >= DocumentState::ComputedScopes)
          bindReferences(*d, *language);
      }
    }
  }
}

void DefaultDocumentBuilder::resetToState(Document& document,
                                          DocumentState state) const {
  // `Changed` destroys the tree: a resolved reference into it is a pointer, so
  // every document that read it lets go first.
  if (state == DocumentState::Changed &&
      document.state.load() != DocumentState::Changed)
    letGoOf({document.id}, false);
  resetDocument(document, state);
}

void DefaultDocumentBuilder::resetDocument(Document& document,
                                           DocumentState state) const {
  // The contract `buildDocuments` owes the lock, checked where it can be
  // broken: past a settle an `Immediate` read may hold this very tree, and
  // dropping a parse is what invalidates every pointer into it.
  assert(!_shared.workspaceLock->settled() &&
         "a document may not be reset once the write has settled");
  IndexManager& index = *_shared.indexManager;
  const CoreServices* language = languageOf(document);
  using enum DocumentState;
  switch (state) {
    case Changed:
      [[fallthrough]];
    case Parsed:
      index.removeContent(document.id);
      [[fallthrough]];
    case IndexedContent:
      document.localSymbols.clear();  // undoes `ComputedScopes`
      [[fallthrough]];
    case ComputedScopes:
      // Undoes `Linked`, and the order is forced: the resolutions are dropped
      // before the descriptions that named them.
      //
      // A memoised resolution is not a rung -- an ordinary read creates one at
      // whatever rung the document happens to be on -- so this runs for every
      // target below `Linked` rather than only when one was reached.
      if (language != nullptr) language->linker->unlink(document);
      [[fallthrough]];
    case Linked:
      index.removeReferences(document.id);
      [[fallthrough]];
    case IndexedReferences:
      document.diagnostics.clear();  // undoes `Validated`...
      {
        // ...and the memory of which categories produced them: the two travel
        // together, or a revalidation would skip what it just erased. The
        // request goes with them: the next build of this document is asked by
        // whoever builds it, not by an attempt that never finished.
        const std::scoped_lock lock(_stateMutex);
        _validated.erase(document.id);
        _buildOptions.erase(document.id);
      }
      [[fallthrough]];
    case Validated:
      break;
  }
  // The tree goes last: `dropParse` is what invalidates every pointer above,
  // and also what makes the text replaceable.
  // There is no root-to-document table: a node climbs to its document, so
  // dropping the tree is dropping the result -- and the local symbols with
  // it, being descriptions into that tree.
  if (state == Changed) document.parseResult = ParseResult{};
  if (document.state.load() > state) document.state = state;
  if (state == ComputedScopes && document.state.load() == ComputedScopes &&
      language != nullptr)
    bindReferences(document, *language);
}

void DefaultDocumentBuilder::computeLocalScopes(
    Document& document, const CoreServices& language,
    const CancellationToken& token) {
  document.localSymbols =
      language.scopeComputation->collectLocalSymbols(document, token);
  bindReferences(document, language);
}

void DefaultDocumentBuilder::bindReferences(Document& document,
                                            const CoreServices& language) {
  // A reference becomes readable at ComputedScopes, including one followed
  // from another document. Install its home before publishing that state;
  // the scope barrier makes every document ready before linking starts. The
  // linker is released last: a reader on another worker that sees it sees
  // the local symbols above.
  if (DocumentReferences* const references =
          document.parseResult.references.get()) {
    references->document.store(&document, std::memory_order_relaxed);
    references->linker.store(language.linker.get(), std::memory_order_release);
  }
}

// What `requested` still owes `document`. An empty request is everything the
// language owes -- the `built_in` half plus every category its registry
// declares -- and that expansion is what lets a later, narrower build be
// compared against what already ran.
std::vector<const ValidationCategory*>
DefaultDocumentBuilder::findMissingValidationCategories(
    const Document& document, const CoreServices& language,
    std::span<const ValidationCategory* const> requested) const {
  std::vector<const ValidationCategory*> wanted;
  const auto want = [&](std::span<const ValidationCategory* const> more) {
    for (const ValidationCategory* category : more)
      if (std::ranges::find(wanted, category) == wanted.end())
        wanted.push_back(category);
  };
  if (requested.empty()) {
    wanted.push_back(&built_in);
    want(language.validationRegistry->categories());
  } else {
    want(requested);
  }
  const std::scoped_lock lock(_stateMutex);
  const auto it = _validated.find(document.id);
  if (it == _validated.end() || it->second.revision != document.buildRevision)
    return wanted;
  std::erase_if(wanted, [&](const ValidationCategory* category) {
    return std::ranges::find(it->second.ran, category) != it->second.ran.end();
  });
  return wanted;
}

// Published as one block: the builder publishes and the validator fills, so
// there is a single place to look for what a diagnostic is made of.
//
// Incrementally: only the categories this request still owes are run, their
// diagnostics are appended to the block already published -- the validator
// re-sorts the whole block, so the order law holds across builds -- and what
// ran is recorded under `_stateMutex`, this being called from parallel bodies.
void DefaultDocumentBuilder::validate(
    Document& document, const CoreServices& language,
    std::span<const ValidationCategory* const> categories,
    const CancellationToken& token) const {
  const std::vector<const ValidationCategory*> missing =
      findMissingValidationCategories(document, language, categories);

  // What an earlier, narrower build already published: a later one appends
  // its categories rather than replacing them.
  std::vector<Diagnostic> all = document.diagnostics;
  if (!missing.empty())
    language.documentValidator->validateDocument(document, all, missing, token);

  // Recorded before the diagnostics are published, which cannot fail: a
  // record that throws publishes nothing, and the next build runs the same
  // categories once, not twice.
  markAsCompleted(document, missing);
  document.diagnostics = std::move(all);
}

// One call to `build` or `update` is one attempt, from its first step. The
// rung goes down before anything can cut the call short: a wait must not read
// the rung of the last build as passed by documents this one resets. Whatever
// fails the call -- a phase, a listener told of the update, the update's own
// resets -- answers the waiters with `BuildFailed`. A cancellation is not a
// failure: the write that supersedes it answers them.
void DefaultDocumentBuilder::runAttempt(
    cursiv::function_ref<void()> body) const {
  const std::uint64_t attempt = beginAttempt();
  try {
    body();
  } catch (const OperationCancelled&) {
    throw;
  } catch (...) {
    publishFailure(attempt);
    throw;
  }
}

std::uint64_t DefaultDocumentBuilder::beginAttempt() const {
  // Two builds never overlap: `build` runs under the workspace lock's
  // exclusivity, so `_buildAttempt` is monotone with nothing to arbitrate.
  const std::scoped_lock lock(_stateMutex);
  _currentState = DocumentState::Changed;
  return ++_buildAttempt;
}

void DefaultDocumentBuilder::wakeDocumentWaiters(
    const Document& document) const {
  if (_documentWaiters.load() == 0) return;
  {
    // Taken: a waiter between its test and its sleep holds it.
    const std::scoped_lock lock(_stateMutex);
    if (std::ranges::find(_awaited, document.id) == _awaited.end()) return;
  }
  _stateCv.notify_all();
}

void DefaultDocumentBuilder::publishWorkspaceState(DocumentState state) const {
  {
    const std::scoped_lock lock(_stateMutex);
    _currentState = state;
    ++_published[static_cast<std::size_t>(state)];
  }
  _stateCv.notify_all();
}

// A build failure is signalled, not carried: the exception is rethrown to
// `build`'s caller, and a waiter only learns the rung will not come. Attempts
// start at one and `_failedAttempt` at zero, so the rank is the flag.
void DefaultDocumentBuilder::publishFailure(std::uint64_t attempt) const {
  {
    const std::scoped_lock lock(_stateMutex);
    _failedAttempt = attempt;
  }
  _stateCv.notify_all();
}

WaitOutcome DefaultDocumentBuilder::waitUntil(
    DocumentState state, const CancellationToken& token) const {
  const auto index = static_cast<std::size_t>(state);
  std::unique_lock lock(_stateMutex);
  if (token.stop_requested()) return WaitOutcome::Cancelled;
  if (_currentState >= state) return WaitOutcome::Reached;
  const std::uint64_t attempt = _buildAttempt;
  const std::uint64_t published = _published[index];
  const bool signalled = _stateCv.wait(lock, token, [&]() {
    return _currentState >= state || _published[index] != published ||
           failedSince(attempt);
  });
  if (!signalled) return WaitOutcome::Cancelled;
  if (_currentState >= state || _published[index] != published)
    return WaitOutcome::Reached;
  return failedSince(attempt) ? WaitOutcome::BuildFailed
                              : WaitOutcome::Cancelled;
}

// A per-document wait ends with the next build that runs to the end or fails:
// reached, cancelled, the build failed, or the workspace passed the rung while
// the document lags -- excluded or removed -- which is `Unreachable`. A
// cancelled build ends none: its successor does, or the waiter's token.
WaitOutcome DefaultDocumentBuilder::waitUntil(
    DocumentState state, DocumentId id, const CancellationToken& token) const {
  // The store is a plain map a write mutates, so it is read under the lock --
  // once, and never from the predicate: a wait is the one place that may not
  // hold the readers' side. The handle then keeps the document alive for the
  // whole wait, and its rung is an atomic.
  std::shared_ptr<Document> document;
  if (!_shared.workspaceLock->read(
          [&] { document = _shared.documents->getDocument(id); }, token,
          WorkspaceLock::ReadPriority::Immediate))
    return WaitOutcome::Cancelled;
  if (document == nullptr) return WaitOutcome::Unreachable;

  std::unique_lock lock(_stateMutex);
  // Cancellation comes first: a cancelled caller has no answer to receive,
  // ready or not.
  if (token.stop_requested()) return WaitOutcome::Cancelled;
  // Counted before the first test of the predicate: a worker that reads no
  // waiter has stored its rung before this count, and the test sees it.
  _awaited.push_back(id);
  _documentWaiters.fetch_add(1);
  // Left with the lock held: the wait takes it back before returning.
  struct Leave {
    std::vector<DocumentId>& awaited;
    std::atomic<std::uint32_t>& count;
    DocumentId id;
    ~Leave() {
      *std::ranges::find(awaited, id) = awaited.back();
      awaited.pop_back();
      count.fetch_sub(1);
    }
  } leave{_awaited, _documentWaiters, id};
  const std::uint64_t attempt = _buildAttempt;
  WaitOutcome outcome = WaitOutcome::Cancelled;
  const auto evaluate = [&]() {
    if (document->state.load() >= state) {
      outcome = WaitOutcome::Reached;
      return true;
    }
    if (failedSince(attempt)) {
      outcome = WaitOutcome::BuildFailed;
      return true;
    }
    if (_currentState >= state) {
      outcome = WaitOutcome::Unreachable;
      return true;
    }
    return false;
  };
  const bool signalled = _stateCv.wait(lock, token, evaluate);
  return signalled ? outcome : WaitOutcome::Cancelled;
}

DocumentState DefaultDocumentBuilder::currentState() const noexcept {
  return _currentState.load();
}

}  // namespace cursiv
