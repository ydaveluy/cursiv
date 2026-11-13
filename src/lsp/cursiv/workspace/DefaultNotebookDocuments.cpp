#include "cursiv/workspace/DefaultNotebookDocuments.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cursiv {

namespace {

// The store takes each new value by a move: one that could fail would leave
// half an update behind.
static_assert(std::is_nothrow_move_assignable_v<lsp::NotebookDocument>);
static_assert(std::is_nothrow_move_assignable_v<std::optional<lsp::LSPObject>>);
static_assert(
    std::is_nothrow_move_assignable_v<std::vector<lsp::NotebookCell>>);

// The URI of each cell, as the index keys it.
std::vector<std::string> uris_of(const std::vector<lsp::NotebookCell>& cells) {
  std::vector<std::string> uris;
  uris.reserve(cells.size());
  for (const lsp::NotebookCell& cell : cells)
    uris.push_back(cell.document.toString());
  return uris;
}

// Each cell named as the notebook `uri`'s.
std::unordered_map<std::string, std::string> index_of(
    const std::string& uri, const std::vector<lsp::NotebookCell>& cells) {
  std::unordered_map<std::string, std::string> index;
  index.reserve(cells.size());
  for (const lsp::NotebookCell& cell : cells)
    index.insert_or_assign(cell.document.toString(), uri);
  return index;
}

}  // namespace

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
  lsp::NotebookDocument notebook = params.notebookDocument;
  CellIndex cells = index_of(uri, notebook.cells);
  const std::scoped_lock lock(_mutex);
  // A notebook opened again replaces the one held, cells and all.
  std::vector<std::string> previous;
  if (const auto held = _notebooks.find(uri); held != _notebooks.end())
    previous = uris_of(held->second.cells);
  _cells.reserve(_cells.size() + cells.size());
  // One insertion, which either happens or leaves the map as it was.
  _notebooks.insert_or_assign(uri, std::move(notebook));
  indexCells(uri, previous, std::move(cells));
}

void DefaultNotebookDocuments::didChange(
    const lsp::DidChangeNotebookDocumentParams& params) {
  const std::string uri = params.notebookDocument.uri.toString();
  const lsp::NotebookDocumentChangeEvent& change = params.change;
  const std::scoped_lock lock(_mutex);
  const auto at = _notebooks.find(uri);
  if (at == _notebooks.end()) return;
  lsp::NotebookDocument& notebook = at->second;

  // What changes is made aside and moved in once nothing is left to fail. Only
  // a change of the structure moves the cells' index.
  std::optional<lsp::LSPObject> metadata = change.metadata;
  std::optional<std::vector<lsp::NotebookCell>> cells;
  const bool restructured =
      change.cells.has_value() && change.cells->structure.has_value();
  std::vector<std::string> previous;
  CellIndex index;
  if (change.cells.has_value() && (restructured || change.cells->data)) {
    std::vector<lsp::NotebookCell>& next = cells.emplace(notebook.cells);
    if (restructured) {
      // Clamped: a change past the end, from a client out of step, edits the
      // tail rather than memory past it.
      const auto& array = change.cells->structure->array;
      const std::size_t start = std::min<std::size_t>(array.start, next.size());
      const std::size_t count =
          std::min<std::size_t>(array.deleteCount, next.size() - start);
      const auto first = next.begin() + static_cast<std::ptrdiff_t>(start);
      const auto inserted =
          next.erase(first, first + static_cast<std::ptrdiff_t>(count));
      if (array.cells.has_value())
        next.insert(inserted, array.cells->begin(), array.cells->end());
      previous = uris_of(notebook.cells);
      index = index_of(uri, next);
      _cells.reserve(_cells.size() + index.size());
    }
    // A cell's kind, metadata or execution summary, matched by its URI.
    if (const auto& data = change.cells->data) {
      std::unordered_map<std::string, const lsp::NotebookCell*> changed;
      for (const lsp::NotebookCell& cell : *data)
        changed[cell.document.toString()] = &cell;
      for (lsp::NotebookCell& cell : next)
        if (const auto found = changed.find(cell.document.toString());
            found != changed.end())
          cell = *found->second;
    }
  }

  notebook.version = params.notebookDocument.version;
  if (metadata.has_value()) notebook.metadata = std::move(metadata);
  if (cells.has_value()) notebook.cells = std::move(*cells);
  if (restructured) indexCells(uri, previous, std::move(index));
}

void DefaultNotebookDocuments::didSave(
    const lsp::DidSaveNotebookDocumentParams&) {
  // the notebook stands as the client last described it
}

void DefaultNotebookDocuments::didClose(
    const lsp::DidCloseNotebookDocumentParams& params) {
  const std::string uri = params.notebookDocument.uri.toString();
  const std::scoped_lock lock(_mutex);
  const auto at = _notebooks.find(uri);
  if (at == _notebooks.end()) return;
  indexCells(uri, uris_of(at->second.cells), {});
  _notebooks.erase(at);
}

void DefaultNotebookDocuments::indexCells(
    std::string_view uri, const std::vector<std::string>& previous,
    CellIndex cells) noexcept {
  for (const std::string& cell : previous)
    if (const auto at = _cells.find(cell);
        at != _cells.end() && at->second == uri)
      _cells.erase(at);
  _cells.merge(cells);
  // A cell another notebook still claims goes to this one, the notebook the
  // client named last.
  for (auto& [cell, owner] : cells)
    _cells.find(cell)->second = std::move(owner);
}

}  // namespace cursiv
