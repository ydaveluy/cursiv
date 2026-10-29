#pragma once

#include <future>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/services/DefaultSharedCoreService.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/BuildOptions.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"

// The scan in three steps: the additional documents first, the folders next,
// one build last. Every service is looked up through `shared` at each call, so
// replacing any of them needs no rebinding here.
//
// `loadAdditionalDocuments` is the one hook, virtual for a use that already
// exists one storey up: a builtin library has no folder to be found in and no
// file to be read from, so it enters the workspace here, on a scheme of its
// own, and a content request then serves its text out of the same store.

namespace cursiv {

class DefaultWorkspaceManager : public WorkspaceManager,
                                protected DefaultSharedCoreService {
 public:
  using DefaultSharedCoreService::DefaultSharedCoreService;
  ~DefaultWorkspaceManager() noexcept override;

  void initializeWorkspace(
      std::span<const WorkspaceFolder> folders,
      const CancellationToken& cancelToken = kNoCancel) override;

  [[nodiscard]] std::span<const WorkspaceFolder> workspaceFolders()
      const noexcept override {
    return _folders;
  }
  [[nodiscard]] std::shared_future<void> ready() const override {
    return _ready;
  }

  // The default policy: hidden entries and the usual build directories out, a
  // file in when the registry claims it. The registry is also the build's
  // routing key, so a file the scan keeps is exactly one the build will route.
  [[nodiscard]] bool shouldIncludeEntry(
      const FileSystemNode& entry) const override;

  // Walks up from the entry to each root that holds it, asking
  // `shouldIncludeEntry` of every folder met, as the scan would on its way
  // down. A workspace folder is `normalize_uri(folder.uri)`, its root
  // `getRootFolder(folder)`.
  [[nodiscard]] bool isIncluded(const FileSystemNode& entry) const override;

  // One walk with an explicit stack so nesting depth does not decide stack use,
  // and a cancellation check per entry. A refused directory is never entered.
  //
  // A folder that refuses enumeration is reported and the walk carries on --
  // a workspace root's refusal as a bootstrap failure, any other folder's as
  // a warning -- one unreadable folder not hiding every document beside it.
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
  // Documents that come from nowhere on disk: a builtin library, a generated
  // prelude. `add` is narrowed to what a hook may legally do at this point.
  //
  // It runs before any folder is walked, so the library wins over a workspace
  // file carrying the same URI, and it runs even for a workspace of no folders
  // -- which is why a client that opened none still gets a scan.
  virtual void loadAdditionalDocuments(
      std::span<const WorkspaceFolder> folders,
      cursiv::function_ref<void(std::string_view uri, std::string text)> add);

  // Where a folder starts. A language whose sources live under a subdirectory
  // returns it here and keeps the walk.
  [[nodiscard]] virtual std::string getRootFolder(
      const WorkspaceFolder& folder) const;

 private:
  BuildOptions _initialBuildOptions;
  std::vector<WorkspaceFolder> _folders;
  // Set exactly once, by the first scan; `_resolved` is only touched inside
  // the write action, and the future is what crosses threads.
  std::promise<void> _readyPromise;
  std::shared_future<void> _ready = _readyPromise.get_future().share();
  bool _resolved = false;
};

}  // namespace cursiv
