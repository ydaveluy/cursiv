#include "cursiv/workspace/DefaultDocuments.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/DocumentId.hpp"
#include "cursiv/workspace/DocumentState.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/IndexManager.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"

namespace cursiv {

namespace {

// A final slash is no second document: the folder index drops it, so both
// spellings are one node there.
std::string_view document_key(std::string_view canonical) noexcept {
  return canonical.ends_with('/') ? canonical.substr(0, canonical.size() - 1)
                                  : canonical;
}

}  // namespace

const DefaultDocuments::Row* DefaultDocuments::row(
    DocumentId id) const noexcept {
  const auto index = static_cast<std::size_t>(id);
  return id == kNoDocument || index >= _rows.size() ? nullptr : &_rows[index];
}

DocumentId DefaultDocuments::getDocumentId(std::string_view uri) const {
  const std::string canonical = normalize_uri(uri);
  const auto it = _byUri.find(document_key(canonical));
  return it == _byUri.end() ? kNoDocument : it->second;
}

DocumentId DefaultDocuments::getOrCreateDocumentId(std::string_view uri) {
  std::string canonical = normalize_uri(uri);
  const std::size_t key = document_key(canonical).size();
  if (const auto it = _byUri.find(std::string_view(canonical).substr(0, key));
      it != _byUri.end())
    return it->second;
  // The last value is `kNoDocument`: an id is refused before it is made, and
  // none is ever handed out twice.
  if (_rows.size() >= static_cast<std::size_t>(kNoDocument))
    throw DocumentStoreError("cursiv: the store has no document id left");
  const auto id = static_cast<DocumentId>(_rows.size());
  _rows.emplace_back(canonical, nullptr);
  _byFolder.insert(canonical, id);
  canonical.resize(key);
  _byUri.try_emplace(std::move(canonical), id);
  return id;
}

std::string DefaultDocuments::getDocumentUri(DocumentId id) const {
  const Row* const r = row(id);
  return r == nullptr ? std::string{} : r->uri;
}

void DefaultDocuments::addDocument(std::shared_ptr<Document> document) {
  if (document == nullptr) return;
  const DocumentId id = getOrCreateDocumentId(document->uri);
  if (_rows[static_cast<std::size_t>(id)].document != nullptr)
    throw DocumentStoreError("cursiv: a document with the URI '" +
                             document->uri + "' is already present");
  document->id = id;
  _rows[static_cast<std::size_t>(id)].document = std::move(document);
}

bool DefaultDocuments::hasDocument(std::string_view uri) const {
  return getDocument(uri) != nullptr;
}

std::shared_ptr<Document> DefaultDocuments::getDocument(
    std::string_view uri) const {
  return getDocument(getDocumentId(uri));
}

std::shared_ptr<Document> DefaultDocuments::getDocument(DocumentId id) const {
  const Row* const r = row(id);
  return r == nullptr ? nullptr : r->document;
}

std::shared_ptr<Document> DefaultDocuments::deleteDocument(DocumentId id) {
  const auto index = static_cast<std::size_t>(id);
  if (id == kNoDocument || index >= _rows.size()) return nullptr;
#ifndef NDEBUG
  // Catches the use-after-free `Documents::deleteDocument` warns about: a
  // dependent's resolved reference into `id`, left by a caller that bypassed
  // `DocumentBuilder::update`, would otherwise dangle silently.
  if (_rows[index].document != nullptr) {
    const DocumentId self[] = {id};
    const bool unreferenced = _shared.indexManager->visitElements(
        nullptr, self, [&](const AstNodeDescription& symbol) {
          return _shared.indexManager->visitReferences(
              id, symbol.symbolId,
              [](const ReferenceDescription&) { return false; });
        });
    assert(unreferenced &&
           "deleteDocument: still referenced by an indexed dependent -- "
           "purge indexes and unlink dependents first (DocumentBuilder::"
           "update), or clear the reference before deleting directly");
  }
#endif
  // The row stays: its id must still name its URI.
  return std::exchange(_rows[index].document, nullptr);
}

std::shared_ptr<Document> DefaultDocuments::deleteDocument(
    std::string_view uri) {
  return deleteDocument(getDocumentId(uri));
}

// The row stays, as it does for one document: an id must still name its URI
// after the document behind it is gone. Only the document is dropped.
std::vector<std::shared_ptr<Document>> DefaultDocuments::getDocuments(
    std::string_view folderUri) const {
  std::vector<std::shared_ptr<Document>> documents;
  for (const DocumentId id : _byFolder.findAll(folderUri))
    if (const Row* const r = row(id); r != nullptr && r->document != nullptr)
      documents.push_back(r->document);
  return documents;
}

std::vector<std::shared_ptr<Document>> DefaultDocuments::deleteDocuments(
    std::string_view folderUri) {
  std::vector<std::shared_ptr<Document>> removed;
  for (const DocumentId id : _byFolder.findAll(folderUri))
    if (std::shared_ptr<Document> gone = deleteDocument(id); gone != nullptr)
      removed.push_back(std::move(gone));
  return removed;
}

std::vector<std::shared_ptr<Document>> DefaultDocuments::all() const {
  std::vector<std::shared_ptr<Document>> documents;
  documents.reserve(_rows.size());
  for (const Row& r : _rows)
    if (r.document != nullptr) documents.push_back(r.document);
  return documents;
}

std::shared_ptr<Document> DefaultDocuments::createDocument(
    std::string_view uri, std::string text, const CancellationToken& token) {
  std::shared_ptr<Document> document =
      _shared.documentFactory->fromString(std::move(text), uri, token);
  addDocument(document);
  return document;
}

std::shared_ptr<Document> DefaultDocuments::getOrCreateDocument(
    std::string_view uri, const CancellationToken& token) {
  assert(!_shared.workspaceLock->mutationEnded() &&
         "getOrCreateDocument mutates the store: a write calls it before its "
         "mutation ends, never in a build phase");
  if (std::shared_ptr<Document> held = getDocument(uri)) return held;
  std::shared_ptr<Document> document =
      _shared.documentFactory->fromUri(uri, token);
  addDocument(document);
  return document;
}

std::shared_ptr<Document> DefaultDocuments::invalidateDocument(
    std::string_view uri) {
  std::shared_ptr<Document> document = getDocument(uri);
  // Through the builder: it owns what a state means, and dropping the derived
  // halves without telling it would leave its own bookkeeping claiming they
  // ran.
  if (document != nullptr)
    _shared.documentBuilder->resetToState(*document, DocumentState::Changed);
  return document;
}

}  // namespace cursiv
