#include "cursiv/workspace/DefaultDocumentFactory.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/workspace/TextDocument.hpp"

namespace cursiv {

const CoreServices& DefaultDocumentFactory::servicesFor(
    std::string_view uri, std::string_view languageId) const {
  return shared.serviceRegistry->getServices(uri, languageId);
}

void DefaultDocumentFactory::dropTree(Document& document) const {
  // What the build derived goes first: the index rows and the references of
  // other documents point into this tree. A document at `Changed` has none,
  // which is every call the builder makes.
  if (document.state.load() != DocumentState::Changed)
    shared.documentBuilder->resetToState(document, DocumentState::Changed);
  resetForRebuild(document);
}

void DefaultDocumentFactory::parse(Document& document,
                                   const CoreServices& services,
                                   const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  // The result views the document's own text, which the document owns and
  // outlives it.
  document.parseResult =
      services.parser->parse(document.textDocument().getText(), cancelToken);
  // The root's parent is the document, which is how a node finds it back.
  if (document.parseResult.value != nullptr)
    document.parseResult.value->parent = &document;
  // Publish only after the result and its owner are installed. The builder
  // resumes at indexing instead of parsing these same bytes a second time.
  document.state = DocumentState::Parsed;
}

std::shared_ptr<Document> DefaultDocumentFactory::create(
    TextDocument textDocument) const {
  const std::string uri = normalize_uri(textDocument.uri());
  const CoreServices& services = servicesFor(uri, textDocument.languageId());
  if (textDocument.languageId().empty())
    textDocument = TextDocument::create(
        std::string(textDocument.uri()), services.languageMetaData.languageId,
        textDocument.version(), std::string(textDocument.getText()));
  const auto document =
      std::make_shared<Document>(std::move(textDocument), uri);
  document->language = &services;
  return document;
}

std::shared_ptr<Document> DefaultDocumentFactory::fromTextDocument(
    TextDocument textDocument, const CancellationToken& cancelToken) const {
  const std::shared_ptr<Document> document = create(std::move(textDocument));
  parse(*document, *document->language, cancelToken);
  return document;
}

std::shared_ptr<Document> DefaultDocumentFactory::fromString(
    std::string text, std::string_view uri,
    const CancellationToken& cancelToken) const {
  const std::string canonical = normalize_uri(uri);
  const CoreServices& services = servicesFor(canonical, {});
  // Disk content is version 0: it has no client to set one.
  return fromTextDocument(
      TextDocument::create(canonical, services.languageMetaData.languageId, 0,
                           std::move(text)),
      cancelToken);
}

std::shared_ptr<Document> DefaultDocumentFactory::fromUri(
    std::string_view uri, const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  const std::string canonical = normalize_uri(uri);
  return fromString(shared.fileSystemProvider->readFile(canonical), canonical,
                    cancelToken);
}

Document& DefaultDocumentFactory::update(
    Document& document, const CancellationToken& cancelToken) const {
  throw_if_cancelled(cancelToken);
  dropTree(document);
  // The truth is the client's buffer while a tab is open, the disk otherwise;
  // a text nothing readable stands behind is kept as it is.
  if (!document.isOpen) {
    try {
      std::string text = shared.fileSystemProvider->readFile(document.uri);
      if (text != document.textDocument().getText()) {
        textDocumentOf(document) = TextDocument::create(
            document.uri, std::string(document.textDocument().languageId()), 0,
            std::move(text));
      }
    } catch (const FileSystemError&) {
      // Nothing readable: keep the current text. A document with none is a file
      // found and then lost, and the reader should know.
      if (document.textDocument().getText().empty())
        shared.observabilitySink->publish(
            {.severity = ObservationSeverity::Warning,
             .code = ObservationCode::WorkspaceDocumentReadFailed,
             .message = "failure to read a new document",
             .uri = document.uri,
             .languageId = {}});
    }
  }
  const CoreServices& services =
      servicesFor(document.uri, document.textDocument().languageId());
  document.language = &services;
  parse(document, services, cancelToken);
  return document;
}

Document& DefaultDocumentFactory::attachVersion(Document& document,
                                                std::int64_t version) const {
  // The one change with no tree to drop: nothing derived from the text moves.
  TextDocument::update(textDocumentOf(document), {}, version);
  return document;
}

Document& DefaultDocumentFactory::attachTextDocument(
    Document& document, TextDocument textDocument) const {
  // The tree goes before the bytes it slices: every view the previous parse
  // handed out points into the text about to be replaced.
  dropTree(document);
  textDocumentOf(document) = std::move(textDocument);
  // The new text carries the client's languageId, which is half of what the
  // language is resolved from.
  document.language =
      &servicesFor(document.uri, document.textDocument().languageId());
  return document;
}

}  // namespace cursiv
