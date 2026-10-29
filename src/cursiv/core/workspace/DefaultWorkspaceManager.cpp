#include "cursiv/core/workspace/DefaultWorkspaceManager.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/observability/ObservabilitySink.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/DocumentState.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceFolder.hpp"

namespace cursiv {

DefaultWorkspaceManager::~DefaultWorkspaceManager() noexcept = default;

void DefaultWorkspaceManager::loadAdditionalDocuments(
    std::span<const WorkspaceFolder>,
    cursiv::function_ref<void(std::string_view, std::string)>) {}

bool DefaultWorkspaceManager::shouldIncludeEntry(
    const FileSystemNode& entry) const {
  const std::string name = uri_file_name(entry.uri);
  // The platform's own hidden entries stay out...
  if (name.empty() || name.front() == '.') return false;
  // ...so do the directories a source tree carries that hold no source of its
  // own: scanning them is the difference between a workspace opening and a
  // workspace hanging...
  if (entry.isDirectory) return name != "node_modules" && name != "out";
  // ...and a file is in exactly when the build could route it.
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
  std::vector<std::string> pending{std::string(folderUri)};
  while (!pending.empty()) {
    const std::string folder = std::move(pending.back());
    pending.pop_back();
    std::vector<FileSystemNode> entries;
    try {
      entries = files.readDirectory(folder);
    } catch (const FileSystemError&) {
      // Said and stepped over, the root's refusal included: one folder the
      // server cannot list must not hide the others. Mute continuation is how
      // a workspace half-opens with no trace. A root is one the workspace
      // was opened on, whoever walks it: a folder an update saw change is
      // not one.
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

void DefaultWorkspaceManager::initializeWorkspace(
    std::span<const WorkspaceFolder> folders,
    const CancellationToken& cancelToken) {
  Documents& store = *shared.documents;
  const DocumentFactory& factory = *shared.documentFactory;

  _folders.assign(folders.begin(), folders.end());

  // A document already in the store is not replaced: what is there is newer
  // than the disk it came from.
  const auto add = [&](std::string_view uri, std::string text) {
    if (store.getDocument(uri) == nullptr)
      store.addDocument(factory.fromString(std::move(text), uri));
  };

  // Ready resolves before the build, and that is the contract: it says the
  // documents are findable, not that they are analysed. Once, whatever later
  // scans do -- a promise keeps no second value to give -- and whatever this
  // one met: a consumer awaiting it would otherwise wait for the session.
  const auto resolveReady = [this] {
    if (_resolved) return;
    _resolved = true;
    _readyPromise.set_value();
  };

  try {
    // (1) What comes from nowhere, first and unconditionally.
    loadAdditionalDocuments(folders, add);

    // (2) The folders. Every kept URI then goes through the one door a URI
    // becomes a document by: the factory normalizes it, and reads the file
    // once. An unreadable file is said and skipped, an unclaimed one skipped
    // -- a scan that died on one permission bit would leave the workspace
    // unanalysed.
    //
    // Uninterruptable, the token deliberately not passed down: this runs once,
    // so a scan cut short leaves documents nothing can name for the session.
    // What the token governs is the build below, which the next one resumes.
    for (const WorkspaceFolder& folder : folders) {
      for (const std::string& uri : searchFolder(getRootFolder(folder))) {
        if (store.getDocument(uri) != nullptr) continue;
        try {
          store.addDocument(factory.fromUri(uri));
        } catch (const FileSystemError&) {
          shared.observabilitySink->publish(
              {.severity = ObservationSeverity::Warning,
               .code = ObservationCode::WorkspaceDocumentReadFailed,
               .message = "failure to read a workspace document",
               .uri = uri,
               .languageId = {}});
        } catch (const ServiceRegistryError&) {
        }
      }
    }
  } catch (...) {
    resolveReady();
    throw;
  }
  resolveReady();

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
                                           DocumentState::IndexedContent);
    all.push_back(document.get());
  }
  shared.documentBuilder->build(all, _initialBuildOptions, cancelToken);
}

}  // namespace cursiv
