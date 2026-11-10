// What the client's explorer does to files (create, rename, delete), asked
// before and told after. A client sends only what `fileOperationOptions()`
// names.
#pragma once

#include <lsp/types.h>

#include <optional>

namespace cursiv {

class FileOperationHandler {
 public:
  virtual ~FileOperationHandler() noexcept = default;

  // The options reported at the handshake. Asked once: routes are registered
  // from them for the whole session.
  [[nodiscard]] virtual const lsp::FileOperationOptions& fileOperationOptions()
      const noexcept = 0;

  // Every arm already has a body: the options are what selects an arm, so
  // a handler that announces two answers two and inherits the rest.
  virtual void didCreateFiles(const lsp::CreateFilesParams&) {
    // nothing to do by default
  }
  virtual void didRenameFiles(const lsp::RenameFilesParams&) {
    // nothing to do by default
  }
  virtual void didDeleteFiles(const lsp::DeleteFilesParams&) {
    // nothing to do by default
  }

  // The edit the client applies, and waits for, before it moves the files, such
  // as a rename's references. `nullopt` sends null. Called under the workspace
  // read lock.
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> willCreateFiles(
      const lsp::CreateFilesParams&) const {
    return std::nullopt;
  }
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> willRenameFiles(
      const lsp::RenameFilesParams&) const {
    return std::nullopt;
  }
  [[nodiscard]] virtual std::optional<lsp::WorkspaceEdit> willDeleteFiles(
      const lsp::DeleteFilesParams&) const {
    return std::nullopt;
  }
};

}  // namespace cursiv
