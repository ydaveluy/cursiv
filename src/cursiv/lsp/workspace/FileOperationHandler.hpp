// What the client's explorer does to the files -- created, renamed,
// deleted -- asked before it happens and told after. Which of the six arms
// a client ever sends is `fileOperationOptions()` alone: an arm the
// options leave out is one nothing will reach.
#pragma once

#include <lsp/types.h>

#include <optional>

namespace cursiv {

class FileOperationHandler {
 public:
  virtual ~FileOperationHandler() noexcept = default;

  /**
   * The options reported to the client at the handshake. Asked once, and
   * the answer must be stable: a route is registered off it for the whole
   * session.
   */
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

  /**
   * The edit the client applies before it moves the files, and waits for:
   * this is where a rename rewrites what names it. `nullopt` means
   * nothing to change, sent as null. Precondition: called under the
   * workspace read lock.
   */
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
