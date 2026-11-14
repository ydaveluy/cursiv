#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/workspace/WorkspaceFolder.hpp"

// What puts documents in the store: the client's folders, enumerated,
// filtered by the registered languages, and built once at the end, so no file
// re-resolves what the one before left broken. No folder at all is valid.

namespace cursiv {

class WorkspaceManager {
 public:
  virtual ~WorkspaceManager() noexcept = default;

  // Scans `folderUris`, adds every claimed document to the store, and builds
  // them together. Like `DocumentBuilder::build`, it does not take the
  // workspace lock: a caller inside a write passes that write's token.
  virtual void initializeWorkspace(
      std::span<const WorkspaceFolder> folders,
      const CancellationToken& cancelToken = kNoCancel) = 0;

  // The folders of the last scan, empty before the first one: what
  // `initializeWorkspace` was given, kept. What a hook needs in order to
  // answer "relative to which root".
  [[nodiscard]] virtual std::span<const WorkspaceFolder> workspaceFolders()
      const noexcept = 0;

  // What the first scan asks, read by that scan alone: `built_in` and `fast`,
  // as an update does by default. Set it before the first scan, for instance
  // to skip validation at startup.
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

  // Whether the scan reaches `entry`: `shouldIncludeEntry` holds for it and for
  // every folder up to its root. A file event is filtered by it. An entry no
  // workspace folder holds is judged alone.
  [[nodiscard]] virtual bool isIncluded(const FileSystemNode& entry) const = 0;

  // Whether the first scan put its documents in the store: they are found,
  // not yet analysed. Wait for analysis with `DocumentBuilder::waitUntil`.
  [[nodiscard]] virtual bool isReady() const noexcept = 0;
  // Waits for it. False when `cancelToken` stops the wait first.
  [[nodiscard]] virtual bool waitUntilReady(
      const CancellationToken& cancelToken = kNoCancel) const = 0;
};

}  // namespace cursiv
