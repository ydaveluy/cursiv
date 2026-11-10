#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/DocumentId.hpp"

// The managed documents of a workspace, by URI and by id; a store only. Every
// entry point normalizes the URI, so two spellings of one file are one id.

namespace cursiv {

struct Document;

// What a store refuses: an error rather than an assertion, so release builds
// still catch a document destroyed while the index describes it.
class DocumentStoreError : public std::invalid_argument {
 public:
  using std::invalid_argument::invalid_argument;
};

class Documents {
 public:
  virtual ~Documents() noexcept = default;

  // Throws when the URI is already held: replacing it would destroy a document
  // the index still describes.
  virtual void addDocument(std::shared_ptr<Document> document) = 0;

  // Built from text and held, in one call. `addDocument` above states the
  // contract this obeys: a URI already held throws rather than being replaced.
  virtual std::shared_ptr<Document> createDocument(
      std::string_view uri, std::string text,
      const CancellationToken& cancelToken = kNoCancel) = 0;

  // The document for `uri`, read from disk if it is not held yet. A mutation of
  // the store like `addDocument`: called from a write before its mutation ends
  // -- never from a build's phases, which read the store unlocked beside it.
  virtual std::shared_ptr<Document> getOrCreateDocument(
      std::string_view uri,
      const CancellationToken& cancelToken = kNoCancel) = 0;

  // Back down to `Changed`, its derived halves dropped, and returned. Null when
  // nothing is held for `uri`.
  virtual std::shared_ptr<Document> invalidateDocument(
      std::string_view uri) = 0;

  [[nodiscard]] virtual bool hasDocument(std::string_view uri) const = 0;
  [[nodiscard]] virtual std::shared_ptr<Document> getDocument(
      std::string_view uri) const = 0;
  [[nodiscard]] virtual std::shared_ptr<Document> getDocument(
      DocumentId id) const = 0;

  // Same URI, same id, forever -- and the id outlives the document, because a
  // description that outlives it must stay nameable.
  [[nodiscard]] virtual DocumentId getDocumentId(
      std::string_view uri) const = 0;
  [[nodiscard]] virtual DocumentId getOrCreateDocumentId(
      std::string_view uri) = 0;
  [[nodiscard]] virtual std::string getDocumentUri(DocumentId id) const = 0;

  // The caller purges the index and unlinks dependents before the returned
  // document dies, as `DocumentBuilder::update` does. Debug builds assert it.
  [[nodiscard]] virtual std::shared_ptr<Document> deleteDocument(
      std::string_view uri) = 0;
  [[nodiscard]] virtual std::shared_ptr<Document> deleteDocument(
      DocumentId id) = 0;

  [[nodiscard]] virtual std::vector<std::shared_ptr<Document>> all() const = 0;

  // Every document under a folder, and the same set removed: a workspace
  // folder that is closed, renamed or deleted moves all of them at once, and
  // asking by URI one at a time means knowing the list first.
  [[nodiscard]] virtual std::vector<std::shared_ptr<Document>> getDocuments(
      std::string_view folderUri) const = 0;
  [[nodiscard]] virtual std::vector<std::shared_ptr<Document>> deleteDocuments(
      std::string_view folderUri) = 0;
};

}  // namespace cursiv
