#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pegium/core/ParseSupport.hpp>
#include <pegium/core/observability/ObservabilitySink.hpp>
#include <pegium/core/parser/Create.hpp>
#include <pegium/core/parser/PegiumParser.hpp>
#include <pegium/core/parser/Parser.hpp>
#include <pegium/core/parser/ParserRule.hpp>
#include <pegium/core/services/CoreServices.hpp>
#include <pegium/core/services/DefaultServiceRegistry.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/core/services/SharedCoreServices.hpp>
#include <pegium/core/utils/UriUtils.hpp>
#include <pegium/core/validation/DocumentValidator.hpp>
#include <pegium/core/workspace/DefaultDocumentFactory.hpp>
#include <pegium/core/workspace/DefaultDocuments.hpp>
#include <pegium/core/workspace/DefaultWorkspaceLock.hpp>
#include <pegium/core/workspace/DocumentFactory.hpp>
#include <pegium/core/workspace/FileSystemProvider.hpp>

/// `pegium::test` is the internal, non-shipped test support for exercising
/// pegium's own internals (CST build, parsing, recovery, services). It is the
/// single namespace for all in-tree fixtures. For feature-level language/LSP
/// tests, prefer the public `pegium::testing` harness. See tests/README.md.
namespace pegium::test {

using namespace pegium::parser;

inline std::string make_file_uri(std::string_view fileName) {
  return utils::path_to_file_uri("/tmp/pegium-tests/" + std::string(fileName));
}

inline bool
wait_until(std::function<bool()> predicate,
           std::chrono::milliseconds timeout = std::chrono::milliseconds(1000),
           std::chrono::milliseconds step = std::chrono::milliseconds(10)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(step);
  }
  return predicate();
}

class RecordingObservabilitySink final
    : public observability::ObservabilitySink {
public:
  void publish(const observability::Observation &observation) noexcept override {
    {
      std::scoped_lock lock(_mutex);
      _observations.push_back(observation);
    }
    _cv.notify_all();
  }

  [[nodiscard]] std::vector<observability::Observation> observations() const {
    std::scoped_lock lock(_mutex);
    return _observations;
  }

  [[nodiscard]] std::optional<observability::Observation>
  lastObservation() const {
    std::scoped_lock lock(_mutex);
    if (_observations.empty()) {
      return std::nullopt;
    }
    return _observations.back();
  }

  [[nodiscard]] bool
  waitForCount(std::size_t count,
               std::chrono::milliseconds timeout =
                   std::chrono::milliseconds(1000)) const {
    std::unique_lock lock(_mutex);
    return _cv.wait_for(lock, timeout,
                        [this, count]() { return _observations.size() >= count; });
  }

private:
  mutable std::mutex _mutex;
  mutable std::condition_variable _cv;
  std::vector<observability::Observation> _observations;
};

class FakeParser final : public parser::Parser {
public:
  using ParseCallback =
      std::function<void(parser::ParseResult &, std::string_view)>;

  FakeParser() : _entryRule("FakeParserEntry", "fake"_kw) {}

  bool fullMatch = true;
  std::vector<parser::ParseDiagnostic> parseDiagnostics;
  parser::ExpectResult expectations;
  ParseCallback callback;
  mutable std::size_t parseCalls = 0;
  mutable std::vector<std::string> parsedTexts;
  mutable std::mutex mutex;

  [[nodiscard]] parser::ParseResult
  parse(text::TextSnapshot text,
        const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    {
      std::scoped_lock lock(mutex);
      ++parseCalls;
      parsedTexts.emplace_back(text.view());
    }
    parser::ParseResult result;
    result.fullMatch = fullMatch;
    result.parsedLength = static_cast<TextOffset>(text.size());
    result.maxCursorOffset = result.parsedLength;
    result.parseDiagnostics = parser::normalizeParseDiagnostics(parseDiagnostics);
    if (callback) {
      callback(result, text.view());
    }
    return result;
  }

  [[nodiscard]] parser::ExpectResult
  expect(std::string_view, TextOffset,
         const utils::CancellationToken &cancelToken) const override {
    utils::throw_if_cancelled(cancelToken);
    return expectations;
  }

  [[nodiscard]] const grammar::ParserRule &
  getEntryRule() const noexcept override {
    return _entryRule;
  }

private:
  parser::ParserRule<AstNode> _entryRule;
};

class FakeFileSystemProvider final : public workspace::FileSystemProvider {
public:
  std::unordered_map<std::string, std::string> files;
  std::unordered_map<std::string, std::vector<std::string>> directories;

  [[nodiscard]] workspace::FileSystemNode
  stat(std::string_view uri) const override {
    const auto key = normalizeKey(uri);
    if (!files.contains(key) && !directories.contains(key)) {
      throw std::runtime_error("Missing file system node: " + key);
    }
    return workspace::FileSystemNode{.isFile = files.contains(key),
                                     .isDirectory = directories.contains(key),
                                     .uri = toFileUri(key)};
  }

  [[nodiscard]] bool exists(std::string_view uri) const override {
    const auto key = normalizeKey(uri);
    return files.contains(key) || directories.contains(key);
  }

  [[nodiscard]] std::vector<std::uint8_t>
  readBinary(std::string_view uri) const override {
    const auto content = readFile(uri);
    return std::vector<std::uint8_t>(content.begin(), content.end());
  }

  [[nodiscard]] std::string readFile(std::string_view uri) const override {
    const auto it = files.find(normalizeKey(uri));
    if (it == files.end()) {
      throw std::runtime_error("Missing file: " + normalizeKey(uri));
    }
    return it->second;
  }

  [[nodiscard]] std::vector<workspace::FileSystemNode>
  readDirectory(std::string_view uri) const override {
    const auto it = directories.find(normalizeKey(uri));
    if (it == directories.end()) {
      throw std::runtime_error("Missing directory: " + normalizeKey(uri));
    }
    std::vector<workspace::FileSystemNode> nodes;
    nodes.reserve(it->second.size());
    for (const auto &child : it->second) {
      nodes.push_back(stat(toFileUri(child)));
    }
    return nodes;
  }

private:
  [[nodiscard]] static std::string normalizeKey(std::string_view uriOrPath) {
    return utils::file_uri_to_path(uriOrPath).value_or(std::string(uriOrPath));
  }

  [[nodiscard]] static std::string toFileUri(std::string_view uriOrPath) {
    if (utils::is_file_uri(uriOrPath)) {
      return std::string(uriOrPath);
    }
    return utils::path_to_file_uri(uriOrPath);
  }
};

class FakeDocumentValidator final : public validation::DocumentValidator {
public:
  std::vector<pegium::Diagnostic> diagnostics;
  mutable std::size_t validateCalls = 0;
  mutable std::vector<validation::ValidationOptions> seenOptions;
  mutable std::vector<pegium::Diagnostic> diagnosticsByCall;
  mutable std::size_t diagnosticIndex = 0;
  mutable std::mutex mutex;

  [[nodiscard]] std::vector<pegium::Diagnostic> validateDocument(
      const workspace::Document &document,
      const validation::ValidationOptions &options,
      const utils::CancellationToken &cancelToken) const override {
    (void)document;
    utils::throw_if_cancelled(cancelToken);
    std::scoped_lock lock(mutex);
    ++validateCalls;
    seenOptions.push_back(options);
    if (!diagnosticsByCall.empty()) {
      const auto index =
          std::min(diagnosticIndex, diagnosticsByCall.size() - 1);
      ++diagnosticIndex;
      return {diagnosticsByCall[index]};
    }
    return diagnostics;
  }
};

inline std::shared_ptr<workspace::TextDocument>
make_text_document(std::string_view uri, std::string languageId,
                   std::string_view text,
                   std::optional<std::int64_t> version = std::nullopt);

class FakeDocumentFactory final : public workspace::DocumentFactory {
public:
  std::unordered_map<std::string, std::string> contentsByUri;
  std::vector<std::string> fromUriCalls;
  std::vector<std::string> fromStringCalls;
  mutable std::size_t updateCalls = 0;
  mutable std::mutex mutex;

  [[nodiscard]] std::shared_ptr<workspace::Document> fromTextDocument(
      std::shared_ptr<workspace::TextDocument> textDocument,
      const utils::CancellationToken &cancelToken = {}) const override {
    utils::throw_if_cancelled(cancelToken);
    assert(textDocument != nullptr);
    auto document =
        std::make_shared<workspace::Document>(std::move(textDocument));
    document->state = workspace::DocumentState::Parsed;
    return document;
  }

  [[nodiscard]] std::shared_ptr<workspace::Document>
  fromString(std::string text, std::string_view uri,
             const utils::CancellationToken &cancelToken = {}) const override {
    utils::throw_if_cancelled(cancelToken);
    auto document = std::make_shared<workspace::Document>(
        make_text_document(uri, {}, std::move(text)));
    document->state = workspace::DocumentState::Parsed;
    return document;
  }

  [[nodiscard]] std::shared_ptr<workspace::Document>
  fromUri(std::string_view uri,
          const utils::CancellationToken &cancelToken = {}) const override {
    utils::throw_if_cancelled(cancelToken);
    const auto it = contentsByUri.find(std::string(uri));
    if (it == contentsByUri.end()) {
      throw std::runtime_error("No content registered for URI: " +
                               std::string(uri));
    }
    auto document = std::make_shared<workspace::Document>(
        make_text_document(std::string(uri), {}, it->second));
    document->state = workspace::DocumentState::Parsed;
    return document;
  }

  workspace::Document &
  update(workspace::Document &document,
         const utils::CancellationToken &cancelToken = {}) const override {
    utils::throw_if_cancelled(cancelToken);
    {
      std::scoped_lock lock(mutex);
      ++updateCalls;
    }
    if (document.state < workspace::DocumentState::Parsed) {
      document.state = workspace::DocumentState::Parsed;
    }
    return document;
  }
};

class InMemoryTextDocuments final : public workspace::TextDocumentProvider {
public:
  [[nodiscard]] std::shared_ptr<workspace::TextDocument>
  getNormalized(std::string_view normalizedUri) const override {
    if (normalizedUri.empty()) {
      return nullptr;
    }

    std::scoped_lock lock(_mutex);
    const auto it = _documents.find(std::string(normalizedUri));
    return it == _documents.end() ? nullptr : it->second;
  }

  [[nodiscard]] bool
  set(std::shared_ptr<workspace::TextDocument> document) {
    assert(document != nullptr);
    document = normalizeDocument(std::move(document));
    if (document == nullptr) {
      return false;
    }

    std::scoped_lock lock(_mutex);
    const bool inserted = !_documents.contains(document->uri());
    _documents.insert_or_assign(document->uri(), std::move(document));
    return inserted;
  }

  void remove(std::string_view uri) {
    const auto normalizedUri = utils::normalize_uri(uri);
    if (normalizedUri.empty()) {
      return;
    }

    std::scoped_lock lock(_mutex);
    _documents.erase(normalizedUri);
  }

private:
  [[nodiscard]] static std::shared_ptr<workspace::TextDocument>
  normalizeDocument(std::shared_ptr<workspace::TextDocument> document) {
    assert(document != nullptr);

    const auto normalizedUri = utils::normalize_uri(document->uri());
    if (normalizedUri.empty()) {
      return nullptr;
    }
    if (normalizedUri == document->uri()) {
      return document;
    }

    return std::make_shared<workspace::TextDocument>(workspace::TextDocument::create(
        normalizedUri, document->languageId(), document->version(),
        std::string(document->getText())));
  }

  mutable std::mutex _mutex;
  std::unordered_map<std::string, std::shared_ptr<workspace::TextDocument>>
      _documents;
};

class RecordingEventDocumentBuilder final : public workspace::DocumentBuilder {
public:
  [[nodiscard]] workspace::BuildOptions &
  updateBuildOptions() noexcept override {
    return _options;
  }

  [[nodiscard]] const workspace::BuildOptions &
  updateBuildOptions() const noexcept override {
    return _options;
  }

  void build(std::span<const std::shared_ptr<workspace::Document>>,
             const workspace::BuildOptions & = {},
             utils::CancellationToken cancelToken = {},
             const std::function<void()> & = {}) const override {
    utils::throw_if_cancelled(cancelToken);
  }

  void update(std::span<const workspace::DocumentId> changedDocumentIds,
              std::span<const workspace::DocumentId> deletedDocumentIds,
              utils::CancellationToken cancelToken = {},
              const std::function<void()> & = {}) const override {
    (void)changedDocumentIds;
    utils::throw_if_cancelled(cancelToken);
    emitUpdate({}, {});
    (void)deletedDocumentIds;
  }

  utils::ScopedDisposable
  onUpdate(std::function<void(std::span<const workspace::DocumentId>,
                              std::span<const workspace::DocumentId>)>
               listener) const override {
    return _onUpdate.on([listener = std::move(listener)](
                            const workspace::DocumentUpdateEvent &event) {
      if (listener) {
        listener(event.changedDocumentIds, event.deletedDocumentIds);
      }
    });
  }

  utils::ScopedDisposable onBuildPhase(
      workspace::DocumentState targetState,
      std::function<void(std::span<const std::shared_ptr<workspace::Document>>,
                         utils::CancellationToken)>
          listener) const override {
    return _onBuildPhase.on(
        [targetState, listener = std::move(listener)](
            const workspace::DocumentBuildPhaseEvent &event) {
          if (listener && event.targetState == targetState) {
            listener(event.builtDocuments, event.cancelToken);
          }
        });
  }

  utils::ScopedDisposable onDocumentPhase(
      workspace::DocumentState targetState,
      std::function<void(const std::shared_ptr<workspace::Document> &,
                         utils::CancellationToken)>
          listener) const override {
    return _onDocumentPhase.on([targetState, listener = std::move(listener)](
                                   const workspace::DocumentPhaseEvent &event) {
      if (listener && event.targetState == targetState) {
        listener(event.builtDocument, event.cancelToken);
      }
    });
  }

  void waitUntil(workspace::DocumentState state,
                 utils::CancellationToken cancelToken = {}) const override {
    (void)state;
    utils::throw_if_cancelled(cancelToken);
  }

  [[nodiscard]] workspace::DocumentId
  waitUntil(workspace::DocumentState state, workspace::DocumentId documentId,
            utils::CancellationToken cancelToken = {}) const override {
    (void)state;
    utils::throw_if_cancelled(cancelToken);
    return documentId;
  }

  void resetToState(workspace::Document &document,
                    workspace::DocumentState state) const override {
    document.state = state;
  }

  void emitUpdate(std::vector<workspace::DocumentId> changedDocumentIds,
                  std::vector<workspace::DocumentId> deletedDocumentIds) const {
    _onUpdate.emit({.changedDocumentIds = std::move(changedDocumentIds),
                    .deletedDocumentIds = std::move(deletedDocumentIds)});
  }

  void emitBuildPhase(
      workspace::DocumentState state,
      std::vector<std::shared_ptr<workspace::Document>> documents = {},
      utils::CancellationToken cancelToken = {}) const {
    _onBuildPhase.emit({.targetState = state,
                        .builtDocuments = std::move(documents),
                        .cancelToken = cancelToken});
  }

  void emitDocumentPhase(workspace::DocumentState state,
                         std::shared_ptr<workspace::Document> document,
                         utils::CancellationToken cancelToken = {}) const {
    assert(document != nullptr);
    _onDocumentPhase.emit({.targetState = state,
                           .builtDocument = std::move(document),
                           .cancelToken = cancelToken});
  }

  [[nodiscard]] std::size_t updateListenerCount() const {
    return _onUpdate.listenerCount();
  }

  [[nodiscard]] std::size_t buildPhaseListenerCount() const {
    return _onBuildPhase.listenerCount();
  }

  [[nodiscard]] std::size_t documentPhaseListenerCount() const {
    return _onDocumentPhase.listenerCount();
  }

  void setUpdatedDocuments(
      std::vector<std::shared_ptr<workspace::Document>> documents) {
    _updatedDocuments = std::move(documents);
  }

private:
  workspace::BuildOptions _options{};
  mutable utils::EventEmitter<workspace::DocumentUpdateEvent> _onUpdate;
  mutable utils::EventEmitter<workspace::DocumentBuildPhaseEvent> _onBuildPhase;
  mutable utils::EventEmitter<workspace::DocumentPhaseEvent> _onDocumentPhase;
  std::vector<std::shared_ptr<workspace::Document>> _updatedDocuments;
};

struct TestCoreServices final : pegium::CoreServices {
  explicit TestCoreServices(const pegium::SharedCoreServices &sharedServices)
      : pegium::CoreServices(sharedServices) {}
};

inline std::unique_ptr<pegium::SharedCoreServices>
make_empty_shared_core_services() {
  auto shared = std::make_unique<pegium::SharedCoreServices>();
  shared->workspace.textDocuments = std::make_shared<InMemoryTextDocuments>();
  return shared;
}

inline std::shared_ptr<InMemoryTextDocuments>
text_documents(pegium::SharedCoreServices &sharedServices) {
  return std::static_pointer_cast<InMemoryTextDocuments>(
      sharedServices.workspace.textDocuments);
}

inline std::shared_ptr<workspace::TextDocument>
make_text_document(std::string_view uri, std::string languageId,
                   std::string_view text,
                   std::optional<std::int64_t> version) {
  return std::make_shared<workspace::TextDocument>(workspace::TextDocument::create(
      utils::normalize_uri(uri), std::move(languageId), version.value_or(0),
      std::string(text)));
}

template <typename DocumentsLike>
inline std::shared_ptr<workspace::TextDocument>
set_text_document(DocumentsLike &documents, std::string uri,
                  std::string languageId, std::string text,
                  std::optional<std::int64_t> version = std::nullopt) {
  const auto normalizedUri = utils::normalize_uri(uri);
  auto textDocument = make_text_document(std::move(uri), std::move(languageId),
                                         std::move(text), version);
  (void)documents.set(textDocument);
  return documents.get(normalizedUri);
}

inline std::unique_ptr<TestCoreServices>
make_uninstalled_core_services(
    const pegium::SharedCoreServices &sharedServices, std::string languageId,
    std::vector<std::string> fileExtensions = {},
    std::vector<std::string> fileNames = {},
    std::unique_ptr<const parser::Parser> parser =
        std::make_unique<FakeParser>()) {
  auto services = std::make_unique<TestCoreServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  services->languageMetaData.fileExtensions = std::move(fileExtensions);
  services->languageMetaData.fileNames = std::move(fileNames);
  services->parser = std::move(parser);
  return services;
}

template <typename ParserType>
inline std::unique_ptr<TestCoreServices>
make_uninstalled_core_services(
    const pegium::SharedCoreServices &sharedServices, std::string languageId,
    std::vector<std::string> fileExtensions = {},
    std::vector<std::string> fileNames = {}) {
  auto services = std::make_unique<TestCoreServices>(sharedServices);
  services->languageMetaData.languageId = std::move(languageId);
  services->languageMetaData.fileExtensions = std::move(fileExtensions);
  services->languageMetaData.fileNames = std::move(fileNames);
  services->parser = std::make_unique<const ParserType>(*services);
  return services;
}

inline std::shared_ptr<workspace::Document>
open_and_build_document(pegium::SharedCoreServices &sharedServices,
                        std::string uri, std::string languageId,
                        std::string text) {
  auto documents = text_documents(sharedServices);
  if (documents == nullptr) {
    return nullptr;
  }
  auto textDocument = set_text_document(*documents, std::move(uri),
                                        std::move(languageId), std::move(text), 1);
  const auto documentId =
      sharedServices.workspace.documents->getOrCreateDocumentId(
          textDocument->uri());
  const std::array<workspace::DocumentId, 1> changedDocumentIds{documentId};
  (void)sharedServices.workspace.documentBuilder->update(changedDocumentIds,
                                                         {});
  return sharedServices.workspace.documents->getDocument(textDocument->uri());
}

} // namespace pegium::test
