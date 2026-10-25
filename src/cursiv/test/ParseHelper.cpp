#include "cursiv/test/ParseHelper.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/workspace/BuildOptions.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/test/ExpectationError.hpp"

namespace cursiv::test {

namespace {

// One counter for the whole process, so two helpers never file two texts under
// one name.
std::atomic<unsigned> nextDocumentId{1};

}  // namespace

std::shared_ptr<Document> parseHelper(const CoreServices& services,
                                      std::string text,
                                      const ParseHelperOptions& options) {
  const SharedCoreServices& shared = services.shared;
  std::string uri = options.documentUri;
  if (uri.empty()) {
    const std::vector<std::string>& extensions =
        services.languageMetaData.fileExtensions;
    uri = "file:///" + std::to_string(nextDocumentId.fetch_add(1));
    if (!extensions.empty()) uri += extensions.front();
  }

  std::shared_ptr<Document> document =
      shared.documentFactory->fromString(std::move(text), uri);
  shared.documents->addDocument(document);
  Document* const set[] = {document.get()};
  shared.documentBuilder->build(set, static_cast<const BuildOptions&>(options));
  return document;
}

std::shared_ptr<Document> parseDocument(const CoreServices& services,
                                        std::string text,
                                        const ParseHelperOptions& options) {
  std::shared_ptr<Document> document =
      parseHelper(services, std::move(text), options);
  if (!document->hasAst())
    throw ExpectationError("cursiv: could not parse document");
  return document;
}

void clearDocuments(const SharedCoreServices& shared,
                    std::span<const std::shared_ptr<Document>> documents) {
  std::vector<std::string> uris;
  if (documents.empty())
    for (const std::shared_ptr<Document>& held : shared.documents->all())
      uris.push_back(held->uri);
  else
    for (const std::shared_ptr<Document>& named : documents)
      uris.push_back(named->uri);

  std::vector<std::string_view> deleted;
  deleted.reserve(uris.size());
  for (const std::string& uri : uris) deleted.emplace_back(uri);
  shared.documentBuilder->update({}, deleted);
}

void clearDocuments(const CoreServices& services,
                    std::span<const std::shared_ptr<Document>> documents) {
  clearDocuments(services.shared, documents);
}

}  // namespace cursiv::test
