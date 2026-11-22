#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/services/DefaultSharedCoreService.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/workspace/WorkspaceManager.hpp"

// The scan: the additional documents, then the folders, then one build.
// Services are looked up through `shared` at each call. The hook
// `loadAdditionalDocuments` adds documents with no folder.

namespace cursiv {

class DefaultWorkspaceManager : public WorkspaceManager,
                                protected DefaultSharedCoreService {
 public:
  enum class ReadErrors { Report, Throw };
  explicit DefaultWorkspaceManager(const SharedCoreServices& services,
                                   ReadErrors readErrors = ReadErrors::Report)
      : DefaultSharedCoreService(services), _readErrors(readErrors) {}
  ~DefaultWorkspaceManager() noexcept override;

  void initializeWorkspace(
      std::span<const WorkspaceFolder> folders,
      const CancellationToken& cancelToken = kNoCancel) override;

  [[nodiscard]] std::span<const WorkspaceFolder> workspaceFolders()
      const noexcept override {
    return _folders;
  }
  [[nodiscard]] bool isReady() const noexcept override {
    return _ready.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool waitUntilReady(
      const CancellationToken& cancelToken = kNoCancel) const override;

  // The default policy: hidden entries and the usual build directories out, a
  // file in when the registry claims it. The registry is also the build's
  // routing key, so a file the scan keeps is exactly one the build will route.
  [[nodiscard]] bool shouldIncludeEntry(
      const FileSystemNode& entry) const override;

  // Walks up from the entry to each root holding it, asking
  // `shouldIncludeEntry` of every folder met, as the scan would going down.
  [[nodiscard]] bool isIncluded(const FileSystemNode& entry) const override;

  // Walks iteratively. ReadErrors controls whether an unreadable entry
  // is reported and skipped or fails the scan.
  [[nodiscard]] std::vector<std::string> searchFolder(
      std::string_view folderUri,
      const CancellationToken& cancelToken = kNoCancel) const override;

  [[nodiscard]] BuildOptions& initialBuildOptions() noexcept override {
    return _initialBuildOptions;
  }
  [[nodiscard]] const BuildOptions& initialBuildOptions()
      const noexcept override {
    return _initialBuildOptions;
  }

 protected:
  // Says the documents are in the store. The first scan calls it once,
  // whatever it met, so that no one waits forever; later calls do nothing.
  void markReady() noexcept;

  // Documents not on disk, such as a builtin library. Runs
  // before any folder, so it wins over a file with the same URI, and runs even
  // with no folder.
  virtual void loadAdditionalDocuments(
      std::span<const WorkspaceFolder> folders,
      cursiv::FunctionRef<void(std::string_view uri, std::string text)> add);

  // Where a folder starts. A language whose sources live under a subdirectory
  // returns it here and keeps the walk.
  [[nodiscard]] virtual std::string getRootFolder(
      const WorkspaceFolder& folder) const;

 private:
  ReadErrors _readErrors;
  BuildOptions _initialBuildOptions = detail::typing_build_options();
  std::vector<WorkspaceFolder> _folders;
  // Set once, by `markReady`; the lock is taken to wait and to wake.
  std::atomic<bool> _ready{false};
  mutable std::mutex _readyMutex;
  mutable std::condition_variable_any _readyCv;
};

}  // namespace cursiv
