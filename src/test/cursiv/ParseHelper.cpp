#include "cursiv/ParseHelper.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/Disposable.hpp"
#include "cursiv/DocumentDisposable.hpp"
#include "cursiv/ExpectationError.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/LanguageMetaData.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/BuildOptions.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv::test {

namespace {

// One counter for the whole process, so two helpers never file two texts under
// one name.
std::atomic<unsigned> nextDocumentId{1};

}  // namespace

std::shared_ptr<Document> parseHelper(const CoreServices& services,
                                      std::string text,
                                      const ParseHelperOptions& options) {
  const SharedCoreServices& shared = services.shared();
  const LanguageMetaData& meta = services.languageMetaData;
  std::string uri = options.documentUri;
  if (uri.empty()) {
    uri = "file:///" + std::to_string(nextDocumentId.fetch_add(1));
    if (!meta.fileExtensions.empty())
      uri += normalizeExtension(meta.fileExtensions.front());
  }

  // Named by its language, which no other one claiming the URI can take.
  std::shared_ptr<Document> document =
      shared.documentFactory->fromTextDocument(TextDocument::create(
          std::move(uri), meta.languageId, 0, std::move(text)));
  detail::build_documents(shared, {&document, 1}, options, kNoCancel);
  return document;
}

void detail::build_documents(
    const SharedCoreServices& shared,
    std::span<const std::shared_ptr<Document>> documents,
    const BuildOptions& options, const CancellationToken& cancel) {
  if (documents.empty()) return;
  std::vector<Document*> batch;
  batch.reserve(documents.size());
  auto rollback =
      Disposable::create([&] { clearDocuments(shared, documents); });
  const DisposeOnThrow cleanup(rollback);
  for (const auto& document : documents) {
    shared.documents->addDocument(document);
    batch.push_back(document.get());
  }
  shared.documentBuilder->build(batch, options, cancel);
}

std::shared_ptr<Document> parseDocument(const CoreServices& services,
                                        std::string text,
                                        const ParseHelperOptions& options) {
  std::shared_ptr<Document> document =
      parseHelper(services, std::move(text), options);
  if (!document->hasAst()) {
    detail::documentDisposable(services, document).dispose();
    throw ExpectationError("cursiv: could not parse document");
  }
  return document;
}

void clearDocuments(const SharedCoreServices& shared,
                    std::span<const std::shared_ptr<Document>> documents) {
  std::vector<std::shared_ptr<Document>> all;
  if (documents.empty()) {
    all = shared.documents->all();
    documents = all;
  }
  std::vector<std::string_view> deleted;
  deleted.reserve(documents.size());
  for (const auto& document : documents)
    if (document && shared.documents->getDocument(document->id) == document)
      deleted.emplace_back(document->uri);
  if (!deleted.empty()) shared.documentBuilder->update({}, deleted);
}

void clearDocuments(const CoreServices& services,
                    std::span<const std::shared_ptr<Document>> documents) {
  clearDocuments(services.shared(), documents);
}

}  // namespace cursiv::test
