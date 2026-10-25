#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "cursiv/core/services/DefaultSharedCoreService.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"

namespace cursiv {

struct CoreServices;

// Not `final`: a derived factory overrides the two steps below -- which
// services a URI belongs to, and how the text becomes a tree.
class DefaultDocumentFactory : public DocumentFactory,
                               protected DefaultSharedCoreService {
 public:
  using DefaultSharedCoreService::DefaultSharedCoreService;

  [[nodiscard]] std::shared_ptr<Document> fromTextDocument(
      TextDocument textDocument,
      const CancellationToken& cancelToken = kNoCancel) const override;

  [[nodiscard]] std::shared_ptr<Document> fromString(
      std::string text, std::string_view uri,
      const CancellationToken& cancelToken = kNoCancel) const override;

  [[nodiscard]] std::shared_ptr<Document> fromModel(
      ParseResult model, std::string_view uri) const override;

  [[nodiscard]] std::shared_ptr<Document> fromUri(
      std::string_view uri,
      const CancellationToken& cancelToken = kNoCancel) const override;

  Document& update(Document& document, const CancellationToken& cancelToken =
                                           kNoCancel) const override;

  Document& attachTextDocument(Document& document,
                               TextDocument textDocument) const override;

  Document& attachVersion(Document& document,
                          std::int64_t version) const override;
  ~DefaultDocumentFactory() noexcept override = default;

 protected:
  // Throws rather than returning null: a caller asking for a document of a
  // language nobody registered has no document to be handed.
  [[nodiscard]] virtual const CoreServices& servicesFor(
      std::string_view uri, std::string_view languageId) const;

  virtual void parse(Document& document, const CoreServices& services,
                     const CancellationToken& cancelToken) const;
};

}  // namespace cursiv
