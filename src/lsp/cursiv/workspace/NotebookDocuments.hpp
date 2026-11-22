// The notebooks the client opened and the notebook of each cell. Installed, it
// makes the server announce notebook sync; each cell is an open text document.
#pragma once

#include <lsp/types.h>

#include <optional>
#include <string_view>

namespace cursiv {

class NotebookDocuments {
 public:
  virtual ~NotebookDocuments() noexcept = default;

  // The notebooks and cell languages the client sends, announced at the
  // handshake. `save` asks for `didSave`.
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

  // Notified before the cells' text changes. A reader may see the notebook
  // before the cells catch up. didChange returns whether to apply the cell
  // changes for this notebook version.
  virtual void didOpen(const lsp::DidOpenNotebookDocumentParams& params) = 0;
  virtual bool didChange(
      const lsp::DidChangeNotebookDocumentParams& params) = 0;
  virtual void didSave(const lsp::DidSaveNotebookDocumentParams& params) = 0;
  virtual void didClose(const lsp::DidCloseNotebookDocumentParams& params) = 0;
};

}  // namespace cursiv
