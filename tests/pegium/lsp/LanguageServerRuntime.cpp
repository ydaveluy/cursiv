#include <gtest/gtest.h>

#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#include <pegium/lsp/LspTestSupport.hpp>
#include <pegium/lsp/workspace/FileOperationHandler.hpp>
#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

namespace pegium {
namespace {

class RecordingRuntimeDocumentUpdateHandler final : public DocumentUpdateHandler {
public:
  [[nodiscard]] bool supportsDidSaveDocument() const noexcept override {
    return true;
  }

  [[nodiscard]] bool supportsWillSaveDocument() const noexcept override {
    return true;
  }

  [[nodiscard]] bool
  supportsWillSaveDocumentWaitUntil() const noexcept override {
    return true;
  }

  void didOpenDocument(const TextDocumentChangeEvent &event) override {
    openedUris.push_back(event.document->uri());
  }

  void didChangeContent(const TextDocumentChangeEvent &event) override {
    changedUris.push_back(event.document->uri());
  }

  void willSaveDocument(const TextDocumentWillSaveEvent &event) override {
    willSaveReasons.push_back(event.reason);
  }

  ::lsp::Array<::lsp::TextEdit>
  willSaveDocumentWaitUntil(const TextDocumentWillSaveEvent &event) override {
    waitUntilReasons.push_back(event.reason);
    ::lsp::TextEdit edit{};
    edit.range.start = text::Position(0, 0);
    edit.range.end = text::Position(0, 0);
    edit.newText = "runtime ";
    return {edit};
  }

  void didSaveDocument(const TextDocumentChangeEvent &event) override {
    savedUris.push_back(event.document->uri());
  }

  void didCloseDocument(const TextDocumentChangeEvent &event) override {
    closedUris.push_back(event.document->uri());
  }

  std::vector<std::string> openedUris;
  std::vector<std::string> changedUris;
  std::vector<std::string> savedUris;
  std::vector<std::string> closedUris;
  std::vector<::lsp::TextDocumentSaveReason> willSaveReasons;
  std::vector<::lsp::TextDocumentSaveReason> waitUntilReasons;
};

class PassiveRuntimeDocumentUpdateHandler final : public DocumentUpdateHandler {
public:
  void
  didChangeWatchedFiles(const ::lsp::DidChangeWatchedFilesParams &params) override {
    watchedChangeCounts.push_back(params.changes.size());
  }

  std::vector<std::size_t> watchedChangeCounts;
};

::lsp::FileOperationOptions make_test_file_operation_options(
    bool includeDidRename = true, bool includeWillDelete = true) {
  ::lsp::FileOperationFilter fileFilter{};
  fileFilter.pattern.glob = "**/*";
  fileFilter.scheme = "file";

  ::lsp::FileOperationRegistrationOptions registration{};
  registration.filters.push_back(std::move(fileFilter));

  ::lsp::FileOperationOptions options{};
  options.didCreate = registration;
  if (includeDidRename) {
    options.didRename = registration;
  }
  if (includeWillDelete) {
    options.willDelete = registration;
  }
  return options;
}

class RecordingRuntimeFileOperationHandler final : public FileOperationHandler {
public:
  [[nodiscard]] const ::lsp::FileOperationOptions &
  fileOperationOptions() const noexcept override {
    return options;
  }

  void didCreateFiles(const ::lsp::CreateFilesParams &params) override {
    didCreateCalls.push_back(params.files.size());
  }

  void didRenameFiles(const ::lsp::RenameFilesParams &params) override {
    didRenameCalls.push_back(params.files.size());
  }

  std::optional<::lsp::WorkspaceEdit>
  willDeleteFiles(const ::lsp::DeleteFilesParams &params) override {
    willDeleteCalls.push_back(params.files.size());
    return ::lsp::WorkspaceEdit{};
  }

  std::vector<std::size_t> didCreateCalls;
  std::vector<std::size_t> didRenameCalls;
  std::vector<std::size_t> willDeleteCalls;

private:
  ::lsp::FileOperationOptions options = make_test_file_operation_options();
};

class LanguageServerRuntimeTest : public ::testing::Test {
protected:
  std::unique_ptr<pegium::SharedServices> shared = test::make_empty_shared_services();

  LanguageServerRuntimeTest() {
    pegium::installDefaultSharedCoreServices(*shared);
    pegium::installDefaultSharedLspServices(*shared);
    pegium::test::initialize_shared_workspace_for_tests(*shared);
  }

  struct MessageHarness {
    test::MemoryStream stream;
    ::lsp::Connection connection;
    ::lsp::MessageHandler handler;
    utils::DisposableStore disposables;

    MessageHarness() : connection(stream), handler(connection) {}
  };

  std::string fileUri(std::string_view fileName) const {
    return test::make_file_uri(fileName);
  }

  std::unique_ptr<MessageHarness> makeHarnessWithDocumentHandler() {
    auto harness = std::make_unique<MessageHarness>();
    addDocumentUpdateHandler(harness->handler, *shared, [] {},
                             harness->disposables);
    return harness;
  }

  std::unique_ptr<MessageHarness>
  makeHarnessWithFileHandler(FileOperationHandler &fileOperationHandler) {
    auto harness = std::make_unique<MessageHarness>();
    addFileOperationHandler(harness->handler, fileOperationHandler, [] {});
    return harness;
  }

  std::unique_ptr<MessageHarness> makeHarnessWithDiagnosticsHandler() {
    auto harness = std::make_unique<MessageHarness>();
    addDiagnosticsHandler(harness->handler, *shared, harness->disposables);
    return harness;
  }
};

TEST_F(LanguageServerRuntimeTest,
       AddDocumentUpdateHandlerWiresTextDocumentEventsToUpdateHandler) {
  auto recordingHandler = std::make_unique<RecordingRuntimeDocumentUpdateHandler>();
  auto *recording = recordingHandler.get();
  shared->lsp.documentUpdateHandler = std::move(recordingHandler);

  auto harness = makeHarnessWithDocumentHandler();

  const auto uri = fileUri("runtime-subscriptions.test");
  ::lsp::DidOpenTextDocumentParams openParams{};
  openParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  openParams.textDocument.languageId = "test";
  openParams.textDocument.version = 1;
  openParams.textDocument.text = "alpha";
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::TextDocument_DidOpen::Method, std::move(openParams)));
  harness->handler.processIncomingMessages();
  ASSERT_EQ(recording->openedUris.size(), 1u);
  ASSERT_EQ(recording->changedUris.size(), 1u);
  EXPECT_EQ(recording->openedUris.front(), uri);
  EXPECT_EQ(recording->changedUris.front(), uri);

  ::lsp::WillSaveTextDocumentParams willSaveParams{};
  willSaveParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  willSaveParams.reason = ::lsp::TextDocumentSaveReason::FocusOut;
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::TextDocument_WillSave::Method,
      std::move(willSaveParams)));
  harness->handler.processIncomingMessages();
  ASSERT_EQ(recording->willSaveReasons.size(), 1u);
  EXPECT_EQ(recording->willSaveReasons.front(),
            ::lsp::TextDocumentSaveReason::FocusOut);

  ::lsp::WillSaveTextDocumentParams waitUntilParams{};
  waitUntilParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  waitUntilParams.reason = ::lsp::TextDocumentSaveReason::AfterDelay;
  harness->stream.pushInput(test::make_request_message(
      1, ::lsp::requests::TextDocument_WillSaveWaitUntil::Method,
      std::move(waitUntilParams)));
  harness->handler.processIncomingMessages();
  EXPECT_NE(harness->stream.written().find("runtime "), std::string::npos);
  ASSERT_EQ(recording->waitUntilReasons.size(), 1u);
  EXPECT_EQ(recording->waitUntilReasons.front(),
            ::lsp::TextDocumentSaveReason::AfterDelay);

  ::lsp::DidSaveTextDocumentParams saveParams{};
  saveParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  saveParams.text = std::string("beta");
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::TextDocument_DidSave::Method, std::move(saveParams)));
  harness->handler.processIncomingMessages();
  ASSERT_EQ(recording->savedUris.size(), 1u);
  EXPECT_EQ(recording->savedUris.front(), uri);

  ::lsp::DidCloseTextDocumentParams closeParams{};
  closeParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::TextDocument_DidClose::Method,
      std::move(closeParams)));
  harness->handler.processIncomingMessages();
  ASSERT_EQ(recording->closedUris.size(), 1u);
  EXPECT_EQ(recording->closedUris.front(), uri);
}

TEST_F(LanguageServerRuntimeTest,
       AddFileOperationHandlerRegistersDeclaredOperations) {
  RecordingRuntimeFileOperationHandler fileOperationHandler;

  auto harness = makeHarnessWithFileHandler(fileOperationHandler);

  ::lsp::CreateFilesParams didCreate{};
  didCreate.files.push_back(::lsp::FileCreate{
      .uri = fileUri("created.test"),
  });
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::Workspace_DidCreateFiles::Method, didCreate));
  harness->handler.processIncomingMessages();

  ::lsp::RenameFilesParams didRename{};
  didRename.files.push_back(::lsp::FileRename{
      .oldUri = fileUri("before.test"),
      .newUri = fileUri("after.test"),
  });
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::Workspace_DidRenameFiles::Method, didRename));
  harness->handler.processIncomingMessages();

  ::lsp::DeleteFilesParams willDelete{};
  willDelete.files.push_back(::lsp::FileDelete{
      .uri = fileUri("deleted.test"),
  });
  harness->stream.pushInput(test::make_request_message(
      1, ::lsp::requests::Workspace_WillDeleteFiles::Method, willDelete));
  harness->handler.processIncomingMessages();

  EXPECT_EQ(fileOperationHandler.didCreateCalls,
            std::vector<std::size_t>{1u});
  EXPECT_EQ(fileOperationHandler.didRenameCalls,
            std::vector<std::size_t>{1u});
  EXPECT_EQ(fileOperationHandler.willDeleteCalls,
            std::vector<std::size_t>{1u});
}

TEST_F(LanguageServerRuntimeTest,
       AddDiagnosticsHandlerSkipsStaleValidatedSnapshotsAndPublishesVersionedOnes) {
  auto builder = std::make_unique<test::RecordingEventDocumentBuilder>();
  auto *builderPtr = builder.get();
  shared->workspace.documentBuilder = std::move(builder);

  auto harness = makeHarnessWithDiagnosticsHandler();
  const auto uri = fileUri("runtime-diagnostics.test");
  ASSERT_NE(test::set_text_document(*shared->lsp.textDocuments, uri, "test",
                                    "beta", 2),
            nullptr);

  auto staleDocument = std::make_shared<workspace::Document>(
      test::make_text_document(uri, "test", "alpha", 1));
  staleDocument->diagnostics.push_back({
      .message = "stale",
      .begin = 0,
      .end = 5,
  });

  builderPtr->emitDocumentPhase(workspace::DocumentState::Validated,
                                staleDocument);
  EXPECT_TRUE(harness->stream.written().empty());

  auto currentDocument = std::make_shared<workspace::Document>(
      test::make_text_document(uri, "test", "beta", 2));
  currentDocument->diagnostics.push_back({
      .message = "current",
      .begin = 0,
      .end = 4,
  });

  builderPtr->emitDocumentPhase(workspace::DocumentState::Validated,
                                currentDocument);

  const auto message = test::parse_last_written_message(harness->stream.written())
                           .object();
  EXPECT_EQ(message.get("method").string(),
            ::lsp::notifications::TextDocument_PublishDiagnostics::Method);
  const auto &params = message.get("params").object();
  EXPECT_EQ(params.get("uri").string(), uri);
  EXPECT_EQ(params.get("version").integer(), 2);
  const auto &diagnostics = params.get("diagnostics").array();
  ASSERT_EQ(diagnostics.size(), 1u);
  EXPECT_EQ(diagnostics.front().object().get("message").string(), "current");
}

TEST_F(LanguageServerRuntimeTest,
       AddDocumentUpdateHandlerUsesNoOpDefaultsForUnimplementedCallbacks) {
  auto handlerImpl = std::make_unique<PassiveRuntimeDocumentUpdateHandler>();
  auto *recording = handlerImpl.get();
  shared->lsp.documentUpdateHandler = std::move(handlerImpl);

  auto harness = makeHarnessWithDocumentHandler();

  const auto uri = fileUri("runtime-passive.test");
  ::lsp::DidOpenTextDocumentParams openParams{};
  openParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  openParams.textDocument.languageId = "test";
  openParams.textDocument.version = 1;
  openParams.textDocument.text = "alpha";
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::TextDocument_DidOpen::Method, std::move(openParams)));

  ::lsp::DidSaveTextDocumentParams saveParams{};
  saveParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  saveParams.text = std::string("beta");
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::TextDocument_DidSave::Method, std::move(saveParams)));

  ::lsp::DidCloseTextDocumentParams closeParams{};
  closeParams.textDocument.uri = ::lsp::DocumentUri(::lsp::Uri::parse(uri));
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::TextDocument_DidClose::Method,
      std::move(closeParams)));
  harness->handler.processIncomingMessages();
  harness->handler.processIncomingMessages();
  harness->handler.processIncomingMessages();

  ::lsp::DidChangeWatchedFilesParams params{};
  params.changes.push_back(::lsp::FileEvent{
      .uri = ::lsp::FileUri(::lsp::Uri::parse(fileUri("watched.test"))),
      .type = ::lsp::FileChangeType::Changed,
  });
  harness->stream.pushInput(test::make_notification_message(
      ::lsp::notifications::Workspace_DidChangeWatchedFiles::Method, params));
  harness->handler.processIncomingMessages();

  EXPECT_EQ(recording->watchedChangeCounts, std::vector<std::size_t>{1u});
}

TEST_F(LanguageServerRuntimeTest,
       StartLanguageServerReturnsFailureWhenLanguageServerIsMissing) {
  test::MemoryStream stream;
  ::lsp::Connection connection(stream);

  shared->lsp.languageServer.reset();

  EXPECT_EQ(startLanguageServer(*shared, connection), 1);
}

TEST_F(LanguageServerRuntimeTest,
       StartLanguageServerProcessesInitializeShutdownAndExitWithExplicitConnection) {
  test::MemoryStream stream;
  ::lsp::Connection connection(stream);

  stream.pushInput(test::with_content_length(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
  stream.pushInput(test::with_content_length(
      R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
  stream.pushInput(
      test::with_content_length(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
  stream.pushInput(
      test::with_content_length(R"({"jsonrpc":"2.0","method":"exit"})"));

  EXPECT_EQ(startLanguageServer(*shared, connection), 0);
  EXPECT_EQ(shared->lsp.languageClient.get(), nullptr);

  const auto messages = test::parse_written_messages(stream.written());
  ASSERT_GE(messages.size(), 2u);
  EXPECT_EQ(messages[0].object().get("id").integer(), 1);
  EXPECT_EQ(messages[1].object().get("id").integer(), 2);
}

// A language extends the shared LSP container with its own workspace-wide
// shared services.
struct ExtendedSharedServices : pegium::SharedServices {
  int extraSharedService = 7;
};

TEST(LanguageServerRuntimeSubclassTest,
     RunsSharedServicesSubclassThroughInitializeShutdownExit) {
  auto shared = std::make_unique<ExtendedSharedServices>();
  pegium::installDefaultSharedCoreServices(*shared);
  pegium::installDefaultSharedLspServices(*shared);
  pegium::test::initialize_shared_workspace_for_tests(*shared);

  test::MemoryStream stream;
  ::lsp::Connection connection(stream);
  stream.pushInput(test::with_content_length(
      R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
  stream.pushInput(test::with_content_length(
      R"({"jsonrpc":"2.0","method":"initialized","params":{}})"));
  stream.pushInput(test::with_content_length(
      R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})"));
  stream.pushInput(
      test::with_content_length(R"({"jsonrpc":"2.0","method":"exit"})"));

  // The subclass container is driven polymorphically by the runtime, and its
  // own shared member is untouched by the session.
  EXPECT_EQ(pegium::startLanguageServer(*shared, connection), 0);
  EXPECT_EQ(shared->extraSharedService, 7);
}

TEST(LanguageServerRuntimeSubclassTest,
     TemplatedEntryInstantiatesForSharedServicesSubclass) {
  // Odr-using the specialization forces its instantiation, proving
  // runLanguageServerMain<Subclass> is well-formed without running a server.
  auto *entry = &pegium::runLanguageServerMain<ExtendedSharedServices>;
  EXPECT_NE(entry, nullptr);
}

} // namespace
} // namespace pegium
