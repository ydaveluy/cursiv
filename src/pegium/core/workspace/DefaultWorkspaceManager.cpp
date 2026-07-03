#include <pegium/core/workspace/DefaultWorkspaceManager.hpp>

#include <cassert>
#include <exception>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include <pegium/core/observability/ObservabilitySink.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/utils/Cancellation.hpp>
#include <pegium/core/utils/UriUtils.hpp>
#include <pegium/core/validation/ValidationRegistry.hpp>

namespace pegium::workspace {

namespace {

void publish_workspace_bootstrap_failure(
    const pegium::SharedCoreServices &sharedServices,
    std::span<const WorkspaceFolder> workspaceFolders, std::string message) {
  observability::Observation observation{
      .severity = observability::ObservationSeverity::Error,
      .code = observability::ObservationCode::WorkspaceBootstrapFailed,
      .message = std::move(message)};
  if (!workspaceFolders.empty()) {
    observation.uri = workspaceFolders.front().uri;
  }
  sharedServices.observabilitySink->publish(observation);
}

} // namespace

DefaultWorkspaceManager::DefaultWorkspaceManager(
    const pegium::SharedCoreServices &sharedServices)
    : pegium::DefaultSharedCoreService(sharedServices) {
  _readyPromise = std::make_shared<std::promise<void>>();
  _readyFuture = _readyPromise->get_future().share();

  _initialBuildOptions = makeDefaultBuildOptions();
}

BuildOptions &DefaultWorkspaceManager::initialBuildOptions() {
  return _initialBuildOptions;
}

const BuildOptions &DefaultWorkspaceManager::initialBuildOptions() const {
  return _initialBuildOptions;
}

std::shared_future<void> DefaultWorkspaceManager::ready() const {
  std::scoped_lock lock(_initMutex);
  return _readyFuture;
}

std::optional<std::vector<WorkspaceFolder>>
DefaultWorkspaceManager::workspaceFolders() const {
  std::scoped_lock lock(_initMutex);
  return _workspaceFolders;
}

void DefaultWorkspaceManager::initialize(const InitializeParams &params) {
  std::scoped_lock lock(_initMutex);
  // Unblock anyone still waiting on the previous, now-abandoned readiness
  // promise before installing a fresh one: a re-initialize would otherwise
  // leave ready() waiters hanging on a promise that is never settled.
  if (_readyPromise && !_readySettled) {
    _readyPromise->set_exception(std::make_exception_ptr(
        std::runtime_error("workspace re-initialized before becoming ready")));
  }
  _readyPromise = std::make_shared<std::promise<void>>();
  _readyFuture = _readyPromise->get_future().share();
  _readySettled = false;
  _workspaceFolders = params.workspaceFolders;
}

std::future<void>
DefaultWorkspaceManager::initialized(const InitializedParams &params) {
  (void)params;

  std::vector<WorkspaceFolder> folders;
  {
    std::scoped_lock lock(_initMutex);
    if (_workspaceFolders.has_value()) {
      folders = *_workspaceFolders;
    }
  }

  return shared.workspace.workspaceLock->write(
      [this, folders = std::move(folders)](
          const utils::CancellationToken &cancelToken,
          const WorkspaceLock::Downgrade &) mutable {
        try {
          utils::throw_if_cancelled(cancelToken);
          initializeWorkspace(folders, cancelToken);
          utils::throw_if_cancelled(cancelToken);
        } catch (const utils::OperationCancelled &) {
          throw;
        } catch (const std::exception &error) {
          publish_workspace_bootstrap_failure(
              shared, folders,
              "Workspace initialization failed: " + std::string(error.what()));
          throw;
        } catch (...) {
          publish_workspace_bootstrap_failure(
              shared, folders, "Workspace initialization failed.");
          throw;
        }
      });
}

void DefaultWorkspaceManager::initializeWorkspace(
    std::span<const WorkspaceFolder> workspaceFolders,
    utils::CancellationToken cancelToken) {
  auto loadedDocuments = performStartup(workspaceFolders, cancelToken);
  utils::throw_if_cancelled(cancelToken);
  assert(shared.workspace.documentBuilder != nullptr);
  shared.workspace.documentBuilder->build(loadedDocuments, _initialBuildOptions,
                                          cancelToken);
}

std::string DefaultWorkspaceManager::getRootFolder(
    const WorkspaceFolder &workspaceFolder) const {
  return workspaceFolder.uri;
}

void DefaultWorkspaceManager::loadAdditionalDocuments(
    std::span<const WorkspaceFolder> workspaceFolders,
    utils::function_ref<void(std::shared_ptr<Document>)> collector,
    utils::CancellationToken cancelToken) {
  (void)workspaceFolders;
  (void)collector;
  utils::throw_if_cancelled(cancelToken);
}

void DefaultWorkspaceManager::loadWorkspaceDocuments(
    std::span<const std::string> workspaceFileUris,
    utils::function_ref<void(std::shared_ptr<Document>)> collector,
    utils::CancellationToken cancelToken) {
  assert(shared.workspace.documents != nullptr);
  auto &documentStore = *shared.workspace.documents;
  for (const auto &fileUri : workspaceFileUris) {
    utils::throw_if_cancelled(cancelToken);
    if (documentStore.getDocument(fileUri) != nullptr) {
      continue;
    }
    auto document = documentStore.getOrCreateDocument(fileUri, cancelToken);
    collector(std::move(document));
  }
}

std::vector<std::shared_ptr<Document>> DefaultWorkspaceManager::performStartup(
    std::span<const WorkspaceFolder> workspaceFolders,
    utils::CancellationToken cancelToken) {
  assert(shared.workspace.documents != nullptr);
  auto &documentStore = *shared.workspace.documents;
  std::vector<std::shared_ptr<Document>> loadedDocuments;

  auto collector = [&documentStore,
                    &loadedDocuments](std::shared_ptr<Document> document) {
    assert(document != nullptr);
    assert(!document->uri.empty());
    if (!documentStore.hasDocument(document->uri)) {
      documentStore.addDocument(document);
    }
    loadedDocuments.push_back(std::move(document));
  };

  try {
    loadAdditionalDocuments(
        workspaceFolders,
        utils::function_ref<void(std::shared_ptr<Document>)>(collector),
        cancelToken);

    std::vector<std::string> workspaceFileUris;
    for (const auto &workspaceFolder : workspaceFolders) {
      utils::throw_if_cancelled(cancelToken);
      const auto rootFolder = utils::normalize_uri(getRootFolder(workspaceFolder));
      if (rootFolder.empty()) {
        continue;
      }
      traverseFolder(*shared.workspace.fileSystemProvider,
                     rootFolder, workspaceFileUris, cancelToken);
    }

    std::unordered_set<std::string> seenUris;
    std::vector<std::string> uniqueWorkspaceFileUris;
    uniqueWorkspaceFileUris.reserve(workspaceFileUris.size());
    for (const auto &fileUri : workspaceFileUris) {
      utils::throw_if_cancelled(cancelToken);
      if (!seenUris.insert(fileUri).second) {
        continue;
      }
      uniqueWorkspaceFileUris.push_back(fileUri);
    }

    loadWorkspaceDocuments(
        uniqueWorkspaceFileUris,
        utils::function_ref<void(std::shared_ptr<Document>)>(collector),
        cancelToken);

    // `ready()` intentionally means startup documents are now discoverable
    // through the workspace, not that the initial build already finished.
    // Callers that need a stable analysis phase must wait on the builder.
    resolveReady();
    return loadedDocuments;
  } catch (...) {
    rejectReady(std::current_exception());
    throw;
  }
}

void DefaultWorkspaceManager::traverseFolder(
    const FileSystemProvider &fileSystem, std::string_view folderUri,
    std::vector<std::string> &workspaceFileUris,
    utils::CancellationToken cancelToken) const {
  utils::throw_if_cancelled(cancelToken);
  try {
    for (const auto &entry : fileSystem.readDirectory(folderUri)) {
      utils::throw_if_cancelled(cancelToken);
      if (!shouldIncludeEntry(entry)) {
        continue;
      }
      if (entry.isDirectory) {
        traverseFolder(fileSystem, entry.uri, workspaceFileUris, cancelToken);
        continue;
      }
      if (entry.isFile) {
        workspaceFileUris.push_back(entry.uri);
      }
    }
  } catch (const utils::OperationCancelled &) {
    throw;
  } catch (const std::exception &error) {
    shared.observabilitySink->publish(observability::Observation{
        .severity = observability::ObservationSeverity::Warning,
        .code = observability::ObservationCode::WorkspaceDirectoryReadFailed,
        .message = "Failure to read directory content of " +
                   std::string(folderUri) + ": " + error.what(),
        .uri = std::string(folderUri)});
  }
}

std::vector<std::string>
DefaultWorkspaceManager::searchFolder(std::string_view workspaceUri) const {
  std::vector<std::string> workspaceFileUris;
  const auto normalizedWorkspaceUri = utils::normalize_uri(workspaceUri);
  if (normalizedWorkspaceUri.empty()) {
    return workspaceFileUris;
  }

  traverseFolder(*shared.workspace.fileSystemProvider,
                 normalizedWorkspaceUri, workspaceFileUris,
                 utils::default_cancel_token);
  return workspaceFileUris;
}

bool DefaultWorkspaceManager::shouldIncludeEntry(
    const FileSystemNode &entry) const {
  const auto path =
      utils::file_uri_to_path(entry.uri).value_or(std::string(entry.uri));
  const auto fileName = std::filesystem::path(path).filename().string();
  if (fileName.empty() || fileName.front() == '.') {
    return false;
  }

  if (entry.isDirectory) {
    return fileName != "node_modules" && fileName != "out";
  }

  if (!entry.isFile) {
    return false;
  }
  return shared.serviceRegistry->findServices(entry.uri) != nullptr;
}

void DefaultWorkspaceManager::resolveReady() {
  std::shared_ptr<std::promise<void>> readyPromise;
  {
    std::scoped_lock lock(_initMutex);
    if (_readySettled) {
      return;
    }
    _readySettled = true;
    readyPromise = _readyPromise;
  }
  assert(readyPromise != nullptr);
  try {
    readyPromise->set_value();
  } catch (const std::future_error &) {
  }
}

void DefaultWorkspaceManager::rejectReady(std::exception_ptr error) {
  std::shared_ptr<std::promise<void>> readyPromise;
  {
    std::scoped_lock lock(_initMutex);
    if (_readySettled) {
      return;
    }
    _readySettled = true;
    readyPromise = _readyPromise;
  }
  assert(readyPromise != nullptr);
  try {
    readyPromise->set_exception(error);
  } catch (const std::future_error &) {
  }
}

} // namespace pegium::workspace
