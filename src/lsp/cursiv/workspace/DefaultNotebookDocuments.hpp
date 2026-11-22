// Each notebook as the client describes it, and the notebook of each cell by
// the cell's URI, behind one mutex.
#pragma once

#include <lsp/types.h>

#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/utils/TransparentStringHash.hpp"
#include "cursiv/workspace/NotebookDocuments.hpp"

namespace cursiv {

class DefaultNotebookDocuments : public NotebookDocuments {
 public:
  explicit DefaultNotebookDocuments(
      lsp::NotebookDocumentSyncOptions options) noexcept
      : _options(std::move(options)) {}
  DefaultNotebookDocuments(const DefaultNotebookDocuments&) = delete;
  DefaultNotebookDocuments& operator=(const DefaultNotebookDocuments&) = delete;

  [[nodiscard]] const lsp::NotebookDocumentSyncOptions&
  notebookDocumentSyncOptions() const noexcept override {
    return _options;
  }

  [[nodiscard]] std::optional<lsp::NotebookDocument> getNotebookDocument(
      std::string_view uri) const override;
  [[nodiscard]] std::optional<lsp::NotebookCell> getNotebookCell(
      std::string_view cellUri) const override;
  [[nodiscard]] std::optional<lsp::NotebookDocument>
  findNotebookDocumentForCell(std::string_view cellUri) const override;

  // Each records what the client sent: an override that reacts calls the base
  // first.
  void didOpen(const lsp::DidOpenNotebookDocumentParams& params) override;
  bool didChange(const lsp::DidChangeNotebookDocumentParams& params) override;
  void didSave(const lsp::DidSaveNotebookDocumentParams& params) override;
  void didClose(const lsp::DidCloseNotebookDocumentParams& params) override;

 private:
  struct CellLocation {
    std::string notebook;
    std::size_t position;
  };
  using CellIndex =
      std::unordered_map<std::string, CellLocation,
                         detail::TransparentStringHash, std::equal_to<>>;
  static CellIndex indexOf(const std::string& uri,
                           const std::vector<lsp::NotebookCell>& cells);

  // Moves the notebook `uri` from its `previous` cells to `cells` without
  // allocating: `_cells` already has room. Callers prepare what may fail first,
  // so a reader never sees half an update.
  void indexCells(std::string_view uri,
                  const std::vector<std::string>& previous,
                  CellIndex cells) noexcept;

  lsp::NotebookDocumentSyncOptions _options;
  mutable std::mutex _mutex;
  std::unordered_map<std::string, lsp::NotebookDocument> _notebooks;
  CellIndex _cells;
};

}  // namespace cursiv
