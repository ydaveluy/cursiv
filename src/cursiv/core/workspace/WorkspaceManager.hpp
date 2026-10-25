#pragma once

#include <future>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/BuildOptions.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceFolder.hpp"

// What puts documents in the store: the folders the client opened,
// enumerated, filtered by the registered languages, and built. Without it a
// cross-document reference would resolve against whatever an editor happens
// to have open. Folders, not a single root -- a client sending none opens a
// workspace of zero folders, which still runs -- and one build at the end:
// added and built one at a time, every file would re-resolve what the
// previous left broken.

namespace cursiv {

class WorkspaceManager {
 public:
  virtual ~WorkspaceManager() noexcept = default;

  // Scans `folderUris`, adds every claimed document to the store, and builds
  // them in one grouped build.
  //
  // The same contract `DocumentBuilder::build` has, and for the same reason: it
  // does not take the workspace lock. A caller inside a write action passes
  // that action's token through; one with no lock passes
  // neither and gets the defaults.
  virtual void initializeWorkspace(
      std::span<const WorkspaceFolder> folders,
      const CancellationToken& cancelToken = kNoCancel) = 0;

  // The folders of the last scan, empty before the first one: what
  // `initializeWorkspace` was given, kept. What a hook needs in order to
  // answer "relative to which root".
  [[nodiscard]] virtual std::span<const WorkspaceFolder> workspaceFolders()
      const noexcept = 0;

  // What the first build of a workspace is asked for. An embedder that wants
  // the startup scan to skip validating sets it here, before the first scan;
  // read by that scan alone, later builds carry their own options.
  [[nodiscard]] virtual BuildOptions& initialBuildOptions() noexcept = 0;
  [[nodiscard]] virtual const BuildOptions& initialBuildOptions()
      const noexcept = 0;

  // Every document URI a folder yields, under `shouldIncludeEntry`. On the
  // interface because a workspace that gets its files from somewhere other than
  // a directory walk -- a manifest, a build system -- replaces this alone.
  [[nodiscard]] virtual std::vector<std::string> searchFolder(
      std::string_view folderUri,
      const CancellationToken& cancelToken = kNoCancel) const = 0;

  // What the scan descends into and what it keeps: the manager's question and
  // never the provider's. A refused directory is never entered.
  [[nodiscard]] virtual bool shouldIncludeEntry(
      const FileSystemNode& entry) const = 0;

  // Resolved once the documents are in the store, which is not "the initial
  // build finished". The two signals are separate on purpose: whoever needs an
  // analysis phase waits for it with `DocumentBuilder::waitUntil`, and merging
  // them would block a consumer on a build the workspace may not owe it.
  //
  // A future and not a flag: a flag can be asked, never awaited, so a consumer
  // on another thread would poll it and race the scan it asked about. The
  // shared future is set exactly once, by the first scan.
  [[nodiscard]] virtual std::shared_future<void> ready() const = 0;
};

}  // namespace cursiv
