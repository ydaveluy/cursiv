#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"

// The managed documents of a workspace, by URI and by id. A store and nothing
// more: what builds a document is the factory's, what parses it is the
// builder's.
//
// Every gate normalizes the URI, so two spellings of one file are one id and
// one document whatever the caller wrote.

namespace cursiv {

struct Document;

// What a store refuses. An error and not an assertion, for the reason a refused
// language registration is one: `-DNDEBUG` would otherwise let the mistake
// through, and the mistake is a document destroyed while the index still
// describes it.
class DocumentStoreError : public std::invalid_argument {
 public:
  using std::invalid_argument::invalid_argument;
};

class Documents {
 public:
  virtual ~Documents() noexcept = default;

  // Throws when the URI is already held. A silent replacement would destroy
  // the document under the index rows that still describe it -- and
  // `deleteDocument` below is where that contract is stated, so this is the
  // door that must not bypass it.
  virtual void addDocument(std::shared_ptr<Document> document) = 0;

  // Built from text and held, in one call. `addDocument` above states the
  // contract this obeys: a URI already held throws rather than being replaced.
  virtual std::shared_ptr<Document> createDocument(
      std::string_view uri, std::string text,
      const CancellationToken& cancelToken = kNoCancel) = 0;

  // The document for `uri`, read from disk if it is not held yet. A mutation of
  // the store like `addDocument`: called from a write, before its settle --
  // never from a build's phases, which read the store unlocked beside it.
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

  // The caller purges indexes and unlinks dependents before letting the
  // returned document die: a dependent still holding a resolved reference
  // into it goes dangling, silently. `DocumentBuilder::update` is the safe
  // route -- it does both before deleting. A debug build asserts this
  // (`DefaultDocuments::deleteDocument`), catching what release would not.
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
