#include "cursiv/workspace/DefaultDocumentBuilder.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cursiv/execution/TaskScheduler.hpp"
#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/references/Linker.hpp"
#include "cursiv/references/ScopeComputation.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/DocumentReferences.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/validation/Diagnostic.hpp"
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
#include "cursiv/workspace/LanguageServiceError.hpp"
#include "cursiv/workspace/TextDocument.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"
#include "cursiv/workspace/WorkspaceManager.hpp"

namespace cursiv {

namespace {

// Calls `tell` on every listener, even after one throws, then rethrows the
// first failure.
template <class Listener, class Tell>
void tell_every(const std::vector<Listener*>& listeners, Tell&& tell) {
  std::exception_ptr failure;
  for (Listener* const listener : listeners) {
    try {
      tell(*listener);
    } catch (...) {
      if (failure == nullptr) failure = std::current_exception();
    }
  }
  if (failure != nullptr) std::rethrow_exception(failure);
}

}  // namespace

DefaultDocumentBuilder::DefaultDocumentBuilder(SharedCoreServices& shared)
    : _shared(shared), _updateBuildOptions(detail::typing_build_options()) {}

std::vector<Document*> DefaultDocumentBuilder::pending(
    std::span<Document* const> all, DocumentState phaseEnd) const {
  std::vector<Document*> work;
  work.reserve(all.size());
  for (Document* d : all)
    if (!d->buildFailed && d->state.load() < phaseEnd &&
        d->language != nullptr &&
        (phaseEnd != DocumentState::Validated ||
         getBuildOptions(*d).validation))
      work.push_back(d);
  // The open ones first, stably: the documents someone is looking at reach
  // every state, and so the client, before the rest of the store.
  std::ranges::stable_partition(work,
                                [](const Document* d) { return d->isOpen; });
  return work;
}

void DefaultDocumentBuilder::runPhase(
    std::span<Document* const> all, std::initializer_list<Step> steps,
    const CancellationToken& token,
    std::optional<cursiv::FunctionRef<void()>> beforePublish) {
  const DocumentState phaseEnd = (steps.end() - 1)->state;
  const std::vector<Document*> work = pending(all, phaseEnd);
  // Tested even when there is nothing to do: a workspace whose whole tail is
  // unclaimed would otherwise never look at the token again.
  throwIfCancelled(token);
  // Where each document entered: a state is reported with the documents of
  // this phase that are at it once the phase is over.
  std::vector<DocumentState> entries(work.size());
  for (std::size_t i = 0; i < work.size(); ++i)
    entries[i] = work[i]->state.load();
  const auto run = [&](std::size_t i) {
    Document& d = *work[i];
    for (const Step& step : steps) {
      if (entries[i] >= step.state) continue;
      throwIfCancelled(token);
      try {
        step.body(d, *d.language, token);
      } catch (const detail::LanguageServiceError& error) {
        d.reportBuildFailure(error.what());
      }
      if (d.buildFailed) {
        wakeDocumentWaiters(d);
        break;
      }
      d.state = step.state;
      wakeDocumentWaiters(d);
      if (!beforePublish) notifyDocumentPhase(d, step.state, token);
    }
  };
  const auto indices = std::views::iota(std::size_t{0}, work.size());
  std::exception_ptr failure;
  try {
    // The pool is a required slot: a container without it is refused at
    // registration, so no null check.
    _shared.taskScheduler->parallelFor(token, indices, run);
  } catch (...) {
    if (!beforePublish) throw;
    failure = std::current_exception();
  }
  if (beforePublish) {
    // Published exports can invalidate old links even when this phase failed.
    bool published = true;
    try {
      (*beforePublish)();
    } catch (...) {
      published = false;
      failure = std::current_exception();
    }
    // Each document's states in order, up to the first that fails, as `run`
    // does.
    std::mutex failures;
    const auto tell = [&](std::size_t i) {
      for (const Step& step : steps) {
        if (entries[i] >= step.state || work[i]->state.load() < step.state)
          continue;
        try {
          notifyDocumentPhase(*work[i], step.state, token);
        } catch (...) {
          const std::scoped_lock lock(failures);
          if (failure == nullptr) failure = std::current_exception();
          return;
        }
      }
    };
    if (!_documentPhaseListeners.empty()) {
      // Unpublished, the write does not allow immediate reads yet: told on
      // this thread, whose reads run inline.
      if (published)
        _shared.taskScheduler->parallelFor(kNoCancel, indices, tell);
      else
        for (const std::size_t i : indices) tell(i);
    }
    if (failure != nullptr) std::rethrow_exception(failure);
  }
  throwIfCancelled(token);
  std::vector<Document*> reached;
  reached.reserve(work.size());
  for (const Step& step : steps) {
    reached.clear();
    for (std::size_t i = 0; i < work.size(); ++i)
      if (entries[i] <= step.state && work[i]->state.load() >= step.state)
        reached.push_back(work[i]);
    if (step.state == DocumentState::Validated)
      for (Document* d : all)
        if (d->buildFailed) {
          prepareBuild({&d, 1}, getBuildOptions(*d));
          tell_every(_documentPhaseListeners,
                     [&](DocumentPhaseListener& listener) {
                       listener.onDocumentBuildFailed(*d);
                     });
        }
    publishWorkspaceState(step.state);
    // After the state is published, on this thread: a listener that waits on
    // the state it is told about must not deadlock, and a cancelled phase, gone
    // by exception above, is never reported.
    notifyBuildPhase(reached, step.state, token);
  }
}

void DefaultDocumentBuilder::notifyBuildPhase(
    std::span<Document* const> documents, DocumentState state,
    const CancellationToken& token) {
  tell_every(_phaseListeners, [&](BuildPhaseListener& listener) {
    listener.onBuildPhase(state, documents, token);
  });
}

bool DefaultDocumentBuilder::resultsAreIncomplete(
    const Document& document, const BuildOptions& options) const {
  const CoreServices* const language = document.language;
  if (language == nullptr) return false;
  return options.validation &&
         !findMissingValidationCategories(document, *language,
                                          options.validationCategories)
              .empty();
}

void DefaultDocumentBuilder::cleanUpDeleted(std::string_view uri) {
  Documents& store = *_shared.documents;
  if (const std::shared_ptr<Document> d = store.getDocument(uri))
    resetDocument(*d, DocumentState::Changed);
  (void)store.deleteDocument(uri);
}

void DefaultDocumentBuilder::markAsCompleted(
    const Document& document, std::span<const ValidationCategory* const> ran) {
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
  if (const std::shared_ptr<Document> held =
          _shared.documents->getDocument(changed))
    return {held->uri};

  const FileSystemProvider& files = *_shared.fileSystemProvider;
  const WorkspaceManager& workspace = *_shared.workspaceManager;
  try {
    const FileSystemNode entry = files.stat(changed);
    // The scan's own filter, the folders above the entry included: an event
    // inside a folder the scan leaves out brings nothing in.
    if (!workspace.isIncluded(entry)) return {};
    if (entry.isDirectory) return workspace.searchFolder(changed);
    return {normalizeUri(changed)};
  } catch (const FileSystemError&) {
    // Nothing readable there: a deletion reported as a change, a race with the
    // editor. It touched nothing this workspace can name.
  }
  return {};
}

void DefaultDocumentBuilder::emitUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted) {
  tell_every(_updateListeners, [&](DocumentUpdateListener& listener) {
    listener.onUpdate(changed, deleted);
  });
}

void DefaultDocumentBuilder::notifyDocumentPhase(
    Document& document, DocumentState state, const CancellationToken& token) {
  tell_every(_documentPhaseListeners, [&](DocumentPhaseListener& listener) {
    try {
      listener.onDocumentPhase(document, state, token);
    } catch (const OperationCancelled&) {
      // The phase tests the token itself, once every listener has received.
    }
  });
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
  {
    const std::scoped_lock lock(_stateMutex);
    const auto at = _buildOptions.find(document.id);
    if (at != _buildOptions.end()) return at->second.options;
  }
  return document.language != nullptr
             ? updateBuildOptionsFor(*document.language)
             : _updateBuildOptions;
}

BuildOptions DefaultDocumentBuilder::updateBuildOptionsFor(
    const CoreServices&) const {
  return _updateBuildOptions;
}

void DefaultDocumentBuilder::prepareBuild(std::span<Document* const> documents,
                                          const BuildOptions& options) {
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
                                   const CancellationToken& token) {
  // The index and the requests are keyed by the store's identities.
  for (const Document* const d : documents)
    if (_shared.documents->getDocument(d->id).get() != d)
      throw DocumentStoreError(
          std::format("cursiv: '{}' is built once the store holds it: add it "
                      "with Documents::addDocument",
                      d->uri));
  runAttempt([&] {
    std::vector<std::string_view> uris;
    uris.reserve(documents.size());
    for (Document* d : documents) {
      uris.push_back(d->uri);
      if (completedFailure(*d)) resetDocument(*d, d->state.load());
    }

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
    {
      // Once the subscribers' resets are done, each document takes this
      // request, whatever was left before: a validated one then runs only the
      // categories it lacks.
      const std::scoped_lock lock(_stateMutex);
      for (const Document* const d : documents) _buildOptions.erase(d->id);
    }
    prepareBuild(documents, options);
    std::vector<Document*> work(documents.begin(), documents.end());
    std::unordered_set<const Document*> requested;
    for (std::size_t at = 0; at < held.size(); ++at) {
      Document* const d = held[at].get();
      if (d->state.load() >= before[at]) continue;
      if (requested.empty())
        requested.insert(documents.begin(), documents.end());
      if (requested.insert(d).second) work.push_back(d);
    }
    buildDocuments(std::move(work), token);
  });
}

void DefaultDocumentBuilder::buildDocuments(
    std::optional<std::vector<Document*>> requested,
    const CancellationToken& token) {
  const auto held = _shared.documents->all();
  auto work = requested ? std::move(*requested) : prepareUpdate(held);
  IndexManager& index = *_shared.indexManager;
  const DocumentFactory& factory = *_shared.documentFactory;

  // Parallel phases schedule each document once, in request order.
  std::unordered_set<Document*> scheduled(work.size());
  std::erase_if(work, [&](Document* d) { return !scheduled.insert(d).second; });
  const auto rebuild = [&](Document* d) {
    if (d->state.load() >= DocumentState::IndexedContent) {
      std::optional<BuildOptions> options;
      {
        const std::scoped_lock lock(_stateMutex);
        if (const auto at = _buildOptions.find(d->id);
            at != _buildOptions.end() && !at->second.completed)
          options = at->second.options;
      }
      resetToState(*d, DocumentState::ComputedScopes);
      if (options) prepareBuild({&d, 1}, *options);
    }
    if (scheduled.insert(d).second) work.push_back(d);
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

  // A document `Validated` by a narrower request re-enters the validating phase
  // alone: the tree, the scopes and both index halves stand.
  for (Document* d : work)
    if (d->state.load() == DocumentState::Validated &&
        resultsAreIncomplete(*d, getBuildOptions(*d)))
      lower(*d, DocumentState::IndexedReferences);
  // Below `IndexedReferences` a memoised resolution has no index row to protect
  // it: dropped while the write still excludes every reader, in what this build
  // builds and, when it brings new exports, everywhere.
  for (const auto& heldDocument : held) {
    Document* const d = heldDocument.get();
    if (d->state.load() >= DocumentState::IndexedReferences ||
        (changed.empty() && !scheduled.contains(d)))
      continue;
    if (const CoreServices* language = d->language; language != nullptr) {
      language->linker->unlink(*d);
      lower(*d, DocumentState::ComputedScopes);
    }
  }

  if (!prepareContent) _shared.workspaceLock->allowImmediateReads();

  const auto publishContent = [&] {
    for (const auto& heldDocument : held) {
      auto* const d = heldDocument.get();
      if (changed.contains(d->id)) continue;
      if (d->state.load() <= DocumentState::ComputedScopes &&
          scheduled.contains(d))
        continue;
      if (index.isAffected(*d, changed)) rebuild(d);
    }
    _shared.workspaceLock->allowImmediateReads();
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
               ? std::optional<cursiv::FunctionRef<void()>>{publishContent}
               : std::nullopt);

  try {
    // A barrier before linking: every local scope is written before the first
    // link reads one, so a cross-document lookup never memoizes a false miss.
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
                 detail::language_call(
                     "Linking", [&] { language.linker->link(d, phase); });
               }},
              {DocumentState::IndexedReferences,
               [&](const Document& d, const CoreServices&,
                   const CancellationToken& phase) {
                 index.updateReferences(d, phase);
               }}},
             token);
  } catch (...) {
    // Linked but not indexed is a state the document cannot keep: back to
    // `ComputedScopes`, so the next build relinks it. Its memo is dropped
    // there, before that build allows immediate reads.
    for (Document* d : work)
      if (d->state.load() == DocumentState::Linked)
        lower(*d, DocumentState::ComputedScopes);
    throw;
  }

  runPhase(work,
           {{DocumentState::Validated,
             [this](Document& d, const CoreServices& language,
                    const CancellationToken& phase) {
               // Per document, not per build: each keeps what it was asked.
               const BuildOptions options = getBuildOptions(d);
               validate(d, language, options.validationCategories, phase);
             }}},
           token);

  const std::scoped_lock lock(_stateMutex);
  for (const Document* const d : work)
    if (const auto at = _buildOptions.find(d->id); at != _buildOptions.end()) {
      const DocumentState last = at->second.options.validation
                                     ? DocumentState::Validated
                                     : DocumentState::IndexedReferences;
      if (d->buildFailed || d->state.load() >= last)
        at->second.completed = true;
    }
}

void DefaultDocumentBuilder::update(std::span<const std::string_view> changed,
                                    std::span<const std::string_view> deleted,
                                    const CancellationToken& token) {
  runAttempt([&] {
    applyUpdate(changed, deleted);
    throwIfCancelled(token);
    buildDocuments(std::nullopt, token);
  });
}

void DefaultDocumentBuilder::applyUpdate(
    std::span<const std::string_view> changed,
    std::span<const std::string_view> deleted) {
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
    if (const std::shared_ptr<Document> held = store.getDocument(uri)) {
      gone.push_back(held->uri);
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

  // Broken references first, even with an empty moved set: this is the whole
  // repair path for a reference broken by a symbol that has since appeared
  // somewhere else.
  letGoOf(moved, true);

  // Then the moved documents, fully, before their text is replaced or they are
  // destroyed. An open document at `Parsed` keeps the parse its opening made.
  for (const std::string_view uri : changedUris)
    if (const std::shared_ptr<Document> d = store.getDocument(uri))
      if (!(d->isOpen && d->state == DocumentState::Parsed && !d->buildFailed))
        resetDocument(*d, DocumentState::Changed);
  for (const std::string_view uri : deletedUris) cleanUpDeleted(uri);

  // A changed document the store lacks is installed empty at `Changed` for
  // the parse phase; `findChangedUris` has already dropped what no language
  // claims.
  const DocumentFactory& factory = *_shared.documentFactory;
  for (const std::string_view uri : changedUris)
    if (!store.hasDocument(uri))
      store.addDocument(
          factory.create(TextDocument::create(std::string(uri), {}, 0, {})));

  // Reported once the store reflects it: a subscriber told of a deletion finds
  // the document gone unless it was recreated by the same update, and every
  // changed document stands at `Changed`.
  emitUpdate(changedUris, deletedUris);
}

std::vector<Document*> DefaultDocumentBuilder::prepareUpdate(
    std::span<const std::shared_ptr<Document>> held) {
  // What to build: a state below the last, a request no build finished, a
  // category the update asks and the document lacks. Each takes the update's
  // request, unless a cancelled build left it one to finish.
  std::unordered_set<DocumentId> unfinished;
  {
    const std::scoped_lock lock(_stateMutex);
    for (const auto& [id, request] : _buildOptions)
      if (!request.completed) unfinished.insert(id);
  }
  std::unordered_map<const CoreServices*,
                     std::pair<BuildOptions, std::vector<Document*>>>
      asked;
  std::vector<Document*> work;
  for (const std::shared_ptr<Document>& doc : held) {
    Document* const d = doc.get();
    if (completedFailure(*d)) continue;
    auto [at, fresh] = asked.try_emplace(d->language);
    if (fresh)
      at->second.first = d->language != nullptr
                             ? updateBuildOptionsFor(*d->language)
                             : _updateBuildOptions;
    if (d->state.load() < DocumentState::Validated ||
        unfinished.contains(d->id) ||
        resultsAreIncomplete(*d, at->second.first)) {
      work.push_back(d);
      at->second.second.push_back(d);
    }
  }
  for (const auto& [language, request] : asked)
    prepareBuild(request.second, request.first);
  return work;
}

bool DefaultDocumentBuilder::completedFailure(const Document& document) const {
  if (!document.buildFailed) return false;
  const std::scoped_lock lock(_stateMutex);
  const auto request = _buildOptions.find(document.id);
  return request != _buildOptions.end() && request->second.completed;
}

void DefaultDocumentBuilder::letGoOf(
    const std::unordered_set<DocumentId>& moved, bool repair) {
  using enum DocumentState;
  const IndexManager& index = *_shared.indexManager;
  for (const std::shared_ptr<Document>& doc : _shared.documents->all()) {
    Document* const d = doc.get();
    if (moved.contains(d->id)) continue;
    if ((repair && shouldRelink(*d, moved)) || index.isAffected(*d, moved))
      resetDocument(*d, ComputedScopes);
    else if (!moved.empty() && d->state.load() < IndexedReferences) {
      // Unindexed resolutions have no dependency row to protect their targets.
      if (const CoreServices* language = d->language; language != nullptr) {
        language->linker->unlink(*d);
        lower(*d, ComputedScopes);
      }
    }
  }
}

bool DefaultDocumentBuilder::shouldRelink(
    const Document& document, const std::unordered_set<DocumentId>&) const {
  return document.hasLinkingErrors();
}

void DefaultDocumentBuilder::resetToState(Document& document,
                                          DocumentState state) {
  // `Changed` destroys the tree: a resolved reference into it is a pointer, so
  // every document that read it lets go first.
  if (state == DocumentState::Changed &&
      document.state.load() != DocumentState::Changed)
    letGoOf({document.id}, false);
  resetDocument(document, state);
}

void DefaultDocumentBuilder::resetDocument(Document& document,
                                           DocumentState state) {
  // The lock's contract for `buildDocuments`, checked where it can be
  // broken: once the write allows them, an `Immediate` read may hold this
  // tree, and dropping a parse is what invalidates every pointer into it.
  if (_shared.workspaceLock->immediateReadsAllowed())
    throw std::logic_error(
        "cursiv: a document is reset before its write allows immediate "
        "reads: one build per write, and no reset from a listener");
  IndexManager& index = *_shared.indexManager;
  const CoreServices* language = document.language;
  using enum DocumentState;
  // A document at `Changed` already holds an empty result. Above it, one is
  // made before anything is undone: a refused allocation leaves the document
  // and its index rows as they were.
  std::optional<ParseResult> empty;
  if (state == Changed && document.state.load() != Changed) empty.emplace();
  if (document.buildFailed && state > document.state.load()) return;
  switch (state) {
    case Changed:
      [[fallthrough]];
    case Parsed:
      index.removeContent(document.id);
      [[fallthrough]];
    case IndexedContent:
      // Undoes `ComputedScopes`: without its scopes no reference is readable.
      document.localSymbols.clear();
      if (DocumentReferences* const references =
              document.parseResult.references.get()) {
        references->linker.store(nullptr, std::memory_order_release);
        references->document.store(nullptr, std::memory_order_relaxed);
      }
      [[fallthrough]];
    case ComputedScopes:
      // Undoes `Linked` in a tree that stays, for any target below `Linked`:
      // any read can memoize a resolution, and resolutions go before the
      // descriptions they name.
      if (language != nullptr && state != Changed)
        language->linker->unlink(document);
      [[fallthrough]];
    case Linked:
      index.removeReferences(document.id);
      [[fallthrough]];
    case IndexedReferences:
      document.diagnostics.clear();
      document.buildFailed = false;
      {
        // The next build chooses its categories again.
        const std::scoped_lock lock(_stateMutex);
        _validated.erase(document.id);
        _buildOptions.erase(document.id);
      }
      [[fallthrough]];
    case Validated:
      break;
  }
  // The tree goes last: dropping it invalidates every pointer above, the local
  // symbols included, and makes the text replaceable.
  if (empty) document.parseResult = std::move(*empty);
  lower(document, state);
}

void DefaultDocumentBuilder::lower(Document& document, DocumentState state) {
  if (document.state.load() > state) document.state = state;
  tell_every(_updateListeners, [&](DocumentUpdateListener& listener) {
    listener.onReset(document, state);
  });
}

void DefaultDocumentBuilder::computeLocalScopes(
    Document& document, const CoreServices& language,
    const CancellationToken& token) {
  document.localSymbols = detail::language_call("Computing local scopes", [&] {
    return language.scopeComputation->collectLocalSymbols(document, token);
  });
  document.localSymbols.buildIndex();
  bindReferences(document, language);
}

void DefaultDocumentBuilder::bindReferences(Document& document,
                                            const CoreServices& language) {
  // Makes the references readable from `ComputedScopes` on. The linker is
  // stored last, with release order: a reader that sees it sees the local
  // symbols.
  if (DocumentReferences* const references =
          document.parseResult.references.get()) {
    references->document.store(&document, std::memory_order_relaxed);
    references->linker.store(language.linker.get(), std::memory_order_release);
  }
}

// What `requested` still asks of `document`. An empty request means the
// `kBuiltIn` half and every category of the language's registry.
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
    wanted.push_back(&kBuiltIn);
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

// Runs only the categories the request still needs and appends their
// diagnostics to the published ones; the validator sorts the whole list.
// Called from parallel bodies, so the record is kept under `_stateMutex`.
void DefaultDocumentBuilder::validate(
    Document& document, const CoreServices& language,
    std::span<const ValidationCategory* const> categories,
    const CancellationToken& token) {
  const std::vector<const ValidationCategory*> missing =
      findMissingValidationCategories(document, language, categories);

  // What an earlier, narrower build already published: a later one appends
  // its categories rather than replacing them. The room the last list took is
  // asked at once.
  std::vector<Diagnostic> all;
  all.reserve(document.diagnostics.capacity());
  all = document.diagnostics;
  if (!missing.empty())
    detail::language_call("Validation", [&] {
      language.documentValidator->validateDocument(
          document, all, {.categories = missing}, token);
    });

  // Recorded before the diagnostics are published, which cannot fail: a
  // record that throws publishes nothing, and the next build runs the same
  // categories once, not twice.
  markAsCompleted(document, missing);
  document.diagnostics = std::move(all);
}

// One `build` or `update` call is one attempt. The workspace state drops
// first. A failure answers waiters with `BuildFailed`; a cancellation does
// not, as the write that supersedes it answers them.
void DefaultDocumentBuilder::runAttempt(cursiv::FunctionRef<void()> body) {
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

std::uint64_t DefaultDocumentBuilder::beginAttempt() {
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

void DefaultDocumentBuilder::publishWorkspaceState(DocumentState state) {
  {
    const std::scoped_lock lock(_stateMutex);
    _currentState = state;
    ++_published[static_cast<std::size_t>(state)];
  }
  _stateCv.notify_all();
}

// A build failure is signalled, not carried: the exception is rethrown to
// `build`'s caller, and a waiter only learns the state will not come. Attempts
// start at one and `_failedAttempt` at zero, so the rank is the flag.
void DefaultDocumentBuilder::publishFailure(std::uint64_t attempt) {
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

// A per-document wait ends with the next build that finishes or fails. It is
// `Unreachable` when the workspace passes the state while the document of the
// id lags. A cancelled build ends none.
WaitOutcome DefaultDocumentBuilder::waitUntil(
    DocumentState state, DocumentId id, const CancellationToken& token) const {
  std::shared_ptr<Document> document;
  for (;;) {
    // The store is read under the workspace lock, never from the predicate,
    // and read again before an `Unreachable` answer, for a new document that
    // took the id; the handle keeps the awaited document alive.
    std::shared_ptr<Document> holder;
    if (!_shared.workspaceLock->read(
            [&] { holder = _shared.documents->getDocument(id); }, token,
            WorkspaceLock::ReadPriority::Immediate))
      return WaitOutcome::Cancelled;
    if (holder == nullptr || holder == document)
      return WaitOutcome::Unreachable;
    document = std::move(holder);

    std::unique_lock lock(_stateMutex);
    // Cancellation comes first: a cancelled caller has no answer to receive,
    // ready or not.
    if (token.stop_requested()) return WaitOutcome::Cancelled;
    // Counted before the first test of the predicate: a worker that reads no
    // waiter has stored its state before this count, and the test sees it.
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
      if (document->buildFailed || failedSince(attempt)) {
        outcome = WaitOutcome::BuildFailed;
        return true;
      }
      if (_currentState >= state) {
        outcome = WaitOutcome::Unreachable;
        return true;
      }
      return false;
    };
    if (!_stateCv.wait(lock, token, evaluate)) return WaitOutcome::Cancelled;
    if (outcome != WaitOutcome::Unreachable) return outcome;
  }
}

DocumentState DefaultDocumentBuilder::currentState() const noexcept {
  return _currentState.load();
}

}  // namespace cursiv
