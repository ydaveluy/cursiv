#include "cursiv/workspace/DefaultNotebookDocuments.hpp"

#include <lsp/types.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cursiv {

namespace {

// The store takes each new value by a move: one that could fail would leave
// half an update behind.
static_assert(std::is_nothrow_move_assignable_v<lsp::NotebookDocument>);
static_assert(std::is_nothrow_move_assignable_v<std::optional<lsp::LSPObject>>);
static_assert(std::is_nothrow_move_assignable_v<lsp::NotebookCell>);

// The URI of each cell, as the index keys it.
std::vector<std::string> uris_of(const std::vector<lsp::NotebookCell>& cells) {
  std::vector<std::string> uris;
  uris.reserve(cells.size());
  for (const lsp::NotebookCell& cell : cells)
    uris.push_back(cell.document.toString());
  return uris;
}

}  // namespace

DefaultNotebookDocuments::CellIndex DefaultNotebookDocuments::indexOf(
    const std::string& uri, const std::vector<lsp::NotebookCell>& cells) {
  CellIndex index;
  index.reserve(cells.size());
  for (std::size_t i = 0; i < cells.size(); ++i)
    index.try_emplace(cells[i].document.toString(), uri, i);
  return index;
}

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
  const auto owner = _cells.find(cellUri);
  if (owner == _cells.end()) return std::nullopt;
  const auto& [notebook, position] = owner->second;
  return _notebooks.at(notebook).cells[position];
}

std::optional<lsp::NotebookDocument>
DefaultNotebookDocuments::findNotebookDocumentForCell(
    std::string_view cellUri) const {
  const std::scoped_lock lock(_mutex);
  const auto owner = _cells.find(cellUri);
  if (owner == _cells.end()) return std::nullopt;
  return _notebooks.at(owner->second.notebook);
}

void DefaultNotebookDocuments::didOpen(
    const lsp::DidOpenNotebookDocumentParams& params) {
  const std::string uri = params.notebookDocument.uri.toString();
  lsp::NotebookDocument notebook = params.notebookDocument;
  CellIndex cells = indexOf(uri, notebook.cells);
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

bool DefaultNotebookDocuments::didChange(
    const lsp::DidChangeNotebookDocumentParams& params) {
  const std::string uri = params.notebookDocument.uri.toString();
  const lsp::NotebookDocumentChangeEvent& change = params.change;
  const std::scoped_lock lock(_mutex);
  const auto at = _notebooks.find(uri);
  if (at == _notebooks.end() ||
      params.notebookDocument.version <= at->second.version)
    return false;
  lsp::NotebookDocument& notebook = at->second;

  // Prepare all copies before committing the new notebook version.
  auto metadata = change.metadata;
  std::optional<std::vector<lsp::NotebookCell>> cells;
  std::vector<std::string> previous;
  CellIndex index;
  if (change.cells && change.cells->structure) {
    const auto& array = change.cells->structure->array;
    const auto& held = notebook.cells;
    const std::size_t start = std::min<std::size_t>(array.start, held.size());
    const std::size_t count =
        std::min<std::size_t>(array.deleteCount, held.size() - start);
    auto& next = cells.emplace();
    next.reserve(held.size() - count + (array.cells ? array.cells->size() : 0));
    const auto first = held.begin() + static_cast<std::ptrdiff_t>(start);
    next.insert(next.end(), held.begin(), first);
    if (array.cells)
      next.insert(next.end(), array.cells->begin(), array.cells->end());
    next.insert(next.end(), first + static_cast<std::ptrdiff_t>(count),
                held.end());
    previous = uris_of(held);
    index = indexOf(uri, next);
    _cells.reserve(_cells.size() + index.size());
  }

  std::vector<std::pair<lsp::NotebookCell*, lsp::NotebookCell>> updates;
  if (change.cells && change.cells->data) {
    const auto& positions = cells ? index : _cells;
    auto& target = cells ? *cells : notebook.cells;
    updates.reserve(change.cells->data->size());
    for (const auto& cell : *change.cells->data) {
      const auto pos = positions.find(cell.document.toString());
      if (pos != positions.end() && pos->second.notebook == uri)
        updates.emplace_back(&target[pos->second.position], cell);
    }
  }

  for (auto& [target, cell] : updates) *target = std::move(cell);
  if (metadata) notebook.metadata = std::move(metadata);
  if (cells) {
    notebook.cells = std::move(*cells);
    indexCells(uri, previous, std::move(index));
  }
  notebook.version = params.notebookDocument.version;
  return true;
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
        at != _cells.end() && at->second.notebook == uri)
      _cells.erase(at);
  _cells.merge(cells);
  // A cell another notebook still claims goes to this one, the notebook the
  // client named last.
  for (auto& [cell, owner] : cells)
    _cells.find(cell)->second = std::move(owner);
}

}  // namespace cursiv
