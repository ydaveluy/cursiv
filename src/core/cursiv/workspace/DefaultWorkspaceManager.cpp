#include "cursiv/workspace/DefaultWorkspaceManager.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/execution/TaskScheduler.hpp"
#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/workspace/WorkspaceFolder.hpp"

namespace cursiv {

DefaultWorkspaceManager::~DefaultWorkspaceManager() noexcept = default;

void DefaultWorkspaceManager::loadAdditionalDocuments(
    std::span<const WorkspaceFolder>,
    cursiv::function_ref<void(std::string_view, std::string)>) {
  // no additional document by default: a language adds its library here
}

bool DefaultWorkspaceManager::shouldIncludeEntry(
    const FileSystemNode& entry) const {
  const std::string name = uri_file_name(entry.uri);
  // Hidden entries are out.
  if (name.empty() || name.front() == '.') return false;
  // So are directories that hold no source of their own: scanning them can
  // make opening a workspace hang.
  if (entry.isDirectory) return name != "node_modules" && name != "out";
  // A file is in exactly when the build could route it.
  if (!entry.isFile) return false;
  return shared.serviceRegistry->hasServices(entry.uri);
}

bool DefaultWorkspaceManager::isIncluded(const FileSystemNode& entry) const {
  // The folder holding `uri`, by its last segment.
  const auto parent = [](std::string_view uri) {
    while (uri.ends_with('/')) uri.remove_suffix(1);
    const std::size_t slash = uri.rfind('/');
    return std::string(
        uri.substr(0, slash == std::string_view::npos ? 0 : slash));
  };
  // Held by a workspace folder or by its root: an entry inside a folder but
  // outside its root is one the scan never reaches.
  bool held = false;
  for (const WorkspaceFolder& folder : _folders) {
    const std::string root = getRootFolder(folder);
    const bool inRoot = contains_uri(root, entry.uri);
    if (!inRoot && !contains_uri(normalize_uri(folder.uri), entry.uri))
      continue;
    held = true;
    if (!inRoot) continue;
    // Up to the root, not into it: the scan enters a root whatever its name.
    bool reached = true;
    FileSystemNode step = entry;
    while (reached && !step.uri.empty() && !contains_uri(step.uri, root)) {
      reached = shouldIncludeEntry(step);
      step = {.isFile = false, .isDirectory = true, .uri = parent(step.uri)};
    }
    if (reached) return true;
  }
  return !held && shouldIncludeEntry(entry);
}

std::string DefaultWorkspaceManager::getRootFolder(
    const WorkspaceFolder& folder) const {
  return normalize_uri(folder.uri);
}

std::vector<std::string> DefaultWorkspaceManager::searchFolder(
    std::string_view folderUri, const CancellationToken& cancelToken) const {
  const FileSystemProvider& files = *shared.fileSystemProvider;
  std::vector<std::string> kept;
  std::vector pending{std::string(folderUri)};
  while (!pending.empty()) {
    const std::string folder = std::move(pending.back());
    pending.pop_back();
    std::vector<FileSystemNode> entries;
    try {
      entries = files.readDirectory(folder);
    } catch (const FileSystemError&) {
      // Reported and skipped, a root's refusal included: one folder that cannot
      // be listed must not hide the others. A folder an update saw change is no
      // root.
      const bool root = std::ranges::any_of(
          _folders, [&](const WorkspaceFolder& open) {
            return getRootFolder(open) == normalize_uri(folder);
          });
      shared.observabilitySink->publish(
          {.severity = root ? ObservationSeverity::Error
                            : ObservationSeverity::Warning,
           .code = root ? ObservationCode::WorkspaceBootstrapFailed
                        : ObservationCode::WorkspaceDirectoryReadFailed,
           .message = root ? "failure to read the workspace root"
                           : "failure to read directory content",
           .uri = folder,
           .languageId = {}});
      continue;
    }
    for (const FileSystemNode& entry : entries) {
      throw_if_cancelled(cancelToken);
      if (!shouldIncludeEntry(entry)) continue;
      if (entry.isDirectory)
        pending.push_back(entry.uri);
      else if (entry.isFile)
        kept.push_back(entry.uri);
    }
  }
  return kept;
}

bool DefaultWorkspaceManager::waitUntilReady(
    const CancellationToken& cancelToken) const {
  if (isReady()) return true;
  std::unique_lock lock(_readyMutex);
  return _readyCv.wait(lock, cancelToken, [this] { return isReady(); });
}

void DefaultWorkspaceManager::markReady() noexcept {
  {
    const std::scoped_lock lock(_readyMutex);
    _ready.store(true, std::memory_order_release);
  }
  _readyCv.notify_all();
}

void DefaultWorkspaceManager::initializeWorkspace(
    std::span<const WorkspaceFolder> folders,
    const CancellationToken& cancelToken) {
  Documents& store = *shared.documents;
  const DocumentFactory& factory = *shared.documentFactory;

  // A document already in the store is not replaced: what is there is newer
  // than the disk it came from.
  const auto add = [&](std::string_view uri, std::string text) {
    if (store.getDocument(uri) == nullptr)
      store.addDocument(factory.fromString(std::move(text), uri));
  };


  try {
    _folders.assign(folders.begin(), folders.end());

    // (1) The additional documents, first and always.
    loadAdditionalDocuments(folders, add);

    // (2) The folders. Each file is read and parsed on the pool, then added in
    // walk order; an unreadable or unclaimed file is skipped. Not cancellable:
    // the token governs the build below, which the next build resumes.
    for (const WorkspaceFolder& folder : folders) {
      const std::vector<std::string> uris = searchFolder(getRootFolder(folder));
      std::vector<std::size_t> unheld;
      for (std::size_t i = 0; i < uris.size(); ++i)
        if (store.getDocument(uris[i]) == nullptr) unheld.push_back(i);
      std::vector<std::shared_ptr<Document>> loaded(uris.size());
      std::vector<std::exception_ptr> failures(uris.size());
      shared.taskScheduler->parallelFor(kNoCancel, unheld, [&](std::size_t i) {
        try {
          loaded[i] = factory.fromUri(uris[i]);
        } catch (...) {
          failures[i] = std::current_exception();
        }
      });
      for (std::size_t i = 0; i < uris.size(); ++i) {
        const std::string& uri = uris[i];
        if (store.getDocument(uri) != nullptr) continue;
        try {
          if (failures[i] != nullptr) std::rethrow_exception(failures[i]);
          store.addDocument(std::move(loaded[i]));
        } catch (const FileSystemError&) {
          shared.observabilitySink->publish(
              {.severity = ObservationSeverity::Warning,
               .code = ObservationCode::WorkspaceDocumentReadFailed,
               .message = "failure to read a workspace document",
               .uri = uri,
               .languageId = {}});
        } catch (const ServiceRegistryError&) {
          // no language claims this file: skip it
        }
      }
    }
  } catch (...) {
    markReady();
    throw;
  }
  markReady();

  // (3) One build over the whole store, not over the documents this call
  // added: a document already there and still broken links against what just
  // arrived, which is the point of scanning.
  throw_if_cancelled(cancelToken);
  const std::vector<std::shared_ptr<Document>> held = store.all();
  std::vector<Document*> all;
  all.reserve(held.size());
  for (const std::shared_ptr<Document>& document : held) {
    if (document->state.load() >= DocumentState::IndexedReferences &&
        document->hasLinkingErrors())
      shared.documentBuilder->resetToState(*document,
                                           DocumentState::ComputedScopes);
    all.push_back(document.get());
  }
  shared.documentBuilder->build(all, _initialBuildOptions, cancelToken);
}

}  // namespace cursiv
