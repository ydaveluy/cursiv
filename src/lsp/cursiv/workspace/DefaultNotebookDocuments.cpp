#include "cursiv/workspace/DefaultNotebookDocuments.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cursiv {

std::optional<lsp::NotebookDocument>
DefaultNotebookDocuments::getNotebookDocument(std::string_view uri) const {
  const std::scoped_lock lock(_mutex);
  const auto at = _notebooks.find(std::string(uri));
  if (at == _notebooks.end()) return std::nullopt;
  return at->second;
}

std::optional<lsp::NotebookCell> DefaultNotebookDocuments::getNotebookCell(
    std::string_view cellUri) const {
  const std::scoped_lock lock(_mutex);
  const auto owner = _cells.find(std::string(cellUri));
  if (owner == _cells.end()) return std::nullopt;
  for (const lsp::NotebookCell& cell : _notebooks.at(owner->second).cells)
    if (cell.document.toString() == cellUri) return cell;
  return std::nullopt;
}

std::optional<lsp::NotebookDocument>
DefaultNotebookDocuments::findNotebookDocumentForCell(
    std::string_view cellUri) const {
  const std::scoped_lock lock(_mutex);
  const auto owner = _cells.find(std::string(cellUri));
  if (owner == _cells.end()) return std::nullopt;
  return _notebooks.at(owner->second);
}

void DefaultNotebookDocuments::didOpen(
    const lsp::DidOpenNotebookDocumentParams& params) {
  const std::string uri = params.notebookDocument.uri.toString();
  const std::scoped_lock lock(_mutex);
  mapCells(uri, _notebooks[uri] = params.notebookDocument);
}

void DefaultNotebookDocuments::didChange(
    const lsp::DidChangeNotebookDocumentParams& params) {
  const std::string uri = params.notebookDocument.uri.toString();
  const std::scoped_lock lock(_mutex);
  const auto at = _notebooks.find(uri);
  if (at == _notebooks.end()) return;
  lsp::NotebookDocument& notebook = at->second;
  notebook.version = params.notebookDocument.version;
  const lsp::NotebookDocumentChangeEvent& change = params.change;
  if (change.metadata.has_value()) notebook.metadata = change.metadata;
  if (change.cells.has_value()) {
    if (const auto& structure = change.cells->structure) {
      // Clamped: a change past the end, from a client out of step, edits the
      // tail rather than memory past it.
      std::vector<lsp::NotebookCell>& cells = notebook.cells;
      const std::size_t start =
          std::min<std::size_t>(structure->array.start, cells.size());
      const std::size_t count = std::min<std::size_t>(
          structure->array.deleteCount, cells.size() - start);
      const auto first = cells.begin() + static_cast<std::ptrdiff_t>(start);
      const auto inserted =
          cells.erase(first, first + static_cast<std::ptrdiff_t>(count));
      if (structure->array.cells.has_value())
        cells.insert(inserted, structure->array.cells->begin(),
                     structure->array.cells->end());
    }
    // A cell's kind, metadata or execution summary, matched by its URI.
    if (const auto& data = change.cells->data) {
      std::unordered_map<std::string, const lsp::NotebookCell*> changed;
      for (const lsp::NotebookCell& cell : *data)
        changed[cell.document.toString()] = &cell;
      for (lsp::NotebookCell& cell : notebook.cells)
        if (const auto found = changed.find(cell.document.toString());
            found != changed.end())
          cell = *found->second;
    }
  }
  mapCells(uri, notebook);
}

void DefaultNotebookDocuments::didSave(
    const lsp::DidSaveNotebookDocumentParams&) {
  // the notebook stands as the client last described it
}

void DefaultNotebookDocuments::didClose(
    const lsp::DidCloseNotebookDocumentParams& params) {
  const std::string uri = params.notebookDocument.uri.toString();
  const std::scoped_lock lock(_mutex);
  std::erase_if(_cells, [&](const auto& cell) { return cell.second == uri; });
  _notebooks.erase(uri);
}

void DefaultNotebookDocuments::mapCells(const std::string& uri,
                                        const lsp::NotebookDocument& notebook) {
  std::erase_if(_cells, [&](const auto& cell) { return cell.second == uri; });
  for (const lsp::NotebookCell& cell : notebook.cells)
    _cells[cell.document.toString()] = uri;
}

}  // namespace cursiv
