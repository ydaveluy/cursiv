#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/core/utils/TransparentStringHash.hpp"
#include "cursiv/core/utils/UriTrie.hpp"
#include "cursiv/core/workspace/DocumentId.hpp"
#include "cursiv/core/workspace/Documents.hpp"

// Ids are dense and never reused: the URI table indexes into one vector, and a
// deleted document leaves its row behind so its id still names its URI.

namespace cursiv {

struct SharedCoreServices;

class DefaultDocuments : public Documents {
 public:
  // Holds the container, not the services in it: the factory and the builder
  // are looked up at the moment of use, so replacing either between two builds
  // is honoured by the next.
  explicit DefaultDocuments(SharedCoreServices& shared) noexcept
      : _shared(shared) {}

  void addDocument(std::shared_ptr<Document> document) override;
  std::shared_ptr<Document> createDocument(
      std::string_view uri, std::string text,
      const CancellationToken& cancelToken = kNoCancel) override;
  std::shared_ptr<Document> getOrCreateDocument(
      std::string_view uri,
      const CancellationToken& cancelToken = kNoCancel) override;
  std::shared_ptr<Document> invalidateDocument(std::string_view uri) override;

  [[nodiscard]] bool hasDocument(std::string_view uri) const override;
  [[nodiscard]] std::shared_ptr<Document> getDocument(
      std::string_view uri) const override;
  [[nodiscard]] std::shared_ptr<Document> getDocument(
      DocumentId id) const override;

  [[nodiscard]] DocumentId getDocumentId(std::string_view uri) const override;
  [[nodiscard]] DocumentId getOrCreateDocumentId(std::string_view uri) override;
  [[nodiscard]] std::string getDocumentUri(DocumentId id) const override;

  [[nodiscard]] std::shared_ptr<Document> deleteDocument(
      std::string_view uri) override;
  [[nodiscard]] std::shared_ptr<Document> deleteDocument(
      DocumentId id) override;

  [[nodiscard]] std::vector<std::shared_ptr<Document>> all() const override;
  [[nodiscard]] std::vector<std::shared_ptr<Document>> getDocuments(
      std::string_view folderUri) const override;
  [[nodiscard]] std::vector<std::shared_ptr<Document>> deleteDocuments(
      std::string_view folderUri) override;

 private:
  // One row per id ever minted. `document` is null for a URI that has an id but
  // no document -- one that was deleted, or one only ever named.
  struct Row {
    std::string uri;
    std::shared_ptr<Document> document;
  };

  [[nodiscard]] const Row* row(DocumentId id) const noexcept;

  SharedCoreServices& _shared;
  std::vector<Row> _rows;
  std::unordered_map<std::string, DocumentId, TransparentStringHash,
                     std::equal_to<>>
      _byUri;
  // A second index over the same truth, never a second truth: `_rows` holds the
  // documents, `_byUri` answers one URI in a hash, and this answers a folder --
  // which a flat map only answers by reading every key.
  UriTrie<DocumentId> _byFolder;
};

}  // namespace cursiv
