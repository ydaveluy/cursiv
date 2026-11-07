// The notebooks the client opened: their cells, and the notebook a cell
// belongs to. Installed, it makes the server announce notebook synchronization
// for the notebooks its options select. Each cell is then an open text
// document of its own, built and validated like any other.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <string_view>

namespace cursiv {

class NotebookDocuments {
 public:
  virtual ~NotebookDocuments() noexcept = default;

  /**
   * The notebooks and cell languages the client sends, announced at the
   * handshake. `save` asks for `didSave`.
   */
  [[nodiscard]] virtual const lsp::NotebookDocumentSyncOptions&
  notebookDocumentSyncOptions() const noexcept = 0;

  // Copies of the notebooks as the client last described them; nothing for a
  // URI no open notebook holds. Safe from any thread.
  [[nodiscard]] virtual std::optional<lsp::NotebookDocument>
  getNotebookDocument(std::string_view uri) const = 0;
  [[nodiscard]] virtual std::optional<lsp::NotebookCell> getNotebookCell(
      std::string_view cellUri) const = 0;
  [[nodiscard]] virtual std::optional<lsp::NotebookDocument>
  findNotebookDocumentForCell(std::string_view cellUri) const = 0;

  // The four notifications, each told before the server opens, changes or
  // closes the cells' text, so a build of the cells sees the notebook as the
  // client has it.
  virtual void didOpen(const lsp::DidOpenNotebookDocumentParams& params) = 0;
  virtual void didChange(
      const lsp::DidChangeNotebookDocumentParams& params) = 0;
  virtual void didSave(const lsp::DidSaveNotebookDocumentParams& params) = 0;
  virtual void didClose(const lsp::DidCloseNotebookDocumentParams& params) = 0;
};

}  // namespace cursiv
