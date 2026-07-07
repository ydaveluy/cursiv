#include <pegium/lsp/runtime/LanguageServerRuntime.hpp>

#include <cassert>
#include <charconv>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <lsp/connection.h>
#include <lsp/io/socket.h>
#include <lsp/io/standardio.h>
#include <lsp/io/stream.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#include <pegium/core/workspace/Documents.hpp>
#include <pegium/lsp/services/DefaultLspModule.hpp>
#include <pegium/lsp/support/Diagnostics.hpp>
#include <pegium/lsp/workspace/DocumentUpdateHandler.hpp>
#include <pegium/lsp/workspace/FileOperationHandler.hpp>
#include <pegium/lsp/runtime/internal/LanguageClientFactory.hpp>
#include <pegium/lsp/runtime/LanguageServerHandlerContext.hpp>
#include <pegium/lsp/runtime/LanguageServerRequestHandlers.hpp>
#include <pegium/lsp/runtime/LanguageServerRuntimeState.hpp>
#include <pegium/lsp/workspace/TextDocuments.hpp>
#include <pegium/lsp/workspace/WorkspaceAdapters.hpp>
#include <pegium/lsp/services/SharedServices.hpp>

namespace pegium {

namespace {

std::optional<unsigned short> parse_port_arg(int argc, char **argv) {
  constexpr std::string_view portPrefix = "--port=";

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (!arg.starts_with(portPrefix)) {
      continue;
    }

    unsigned short port = 0;
    const auto portString = arg.substr(portPrefix.size());
    const auto [ptr, ec] = std::from_chars(
        portString.data(), portString.data() + portString.size(), port);
    (void)ptr;
    if (ec == std::errc{}) {
      return port;
    }
  }

  return std::nullopt;
}

void ignore_sigpipe_process_wide() {
#ifndef _WIN32
  static const bool ignored = []() {
    std::signal(SIGPIPE, SIG_IGN);
    return true;
  }();
  (void)ignored;
#endif
}

void ensure_initialized(const LanguageServerRuntimeState &runtimeState) {
  if (!runtimeState.initialized()) {
    throw ::lsp::RequestError(
        static_cast<int>(::lsp::ErrorCodes::ServerNotInitialized),
        "Server not initialized");
  }
}

::lsp::TextDocumentSaveReason
to_lsp_save_reason(workspace::TextDocumentSaveReason reason) {
  using enum workspace::TextDocumentSaveReason;
  switch (reason) {
  case AfterDelay:
    return ::lsp::TextDocumentSaveReason::AfterDelay;
  case FocusOut:
    return ::lsp::TextDocumentSaveReason::FocusOut;
  case Manual:
  default:
    return ::lsp::TextDocumentSaveReason::Manual;
  }
}

workspace::TextEdit
to_workspace_text_edit(const ::lsp::TextEdit &edit) {
  return workspace::TextEdit{
      .range = text::Range(edit.range.start, edit.range.end),
      .newText = edit.newText,
  };
}

/// Resolves cross-file related-information ranges: tries the live LSP
/// text-document cache first (freshest content for files open in the editor),
/// then falls back to the workspace-wide document registry, which also covers
/// files that have been indexed but are not open in the editor.
class CrossFileTextDocumentProvider final
    : public workspace::TextDocumentProvider {
public:
  CrossFileTextDocumentProvider(const TextDocuments *openBuffers,
                                const workspace::Documents *documents)
      : _openBuffers(openBuffers), _documents(documents) {}

  [[nodiscard]] std::shared_ptr<workspace::TextDocument>
  getNormalized(std::string_view normalizedUri) const override {
    if (_openBuffers != nullptr) {
      if (auto open = _openBuffers->getNormalized(normalizedUri);
          open != nullptr) {
        return open;
      }
    }
    if (_documents != nullptr) {
      if (auto document = _documents->getDocument(normalizedUri);
          document != nullptr) {
        // `Documents` exposes only a const view of its TextDocument; this
        // adapter is read-only (used solely to compute a cross-file
        // position), so aliasing through a const_cast is safe here.
        return {document, const_cast<workspace::TextDocument *>(
                              &document->textDocument())};
      }
    }
    return nullptr;
  }

private:
  const TextDocuments *_openBuffers;
  const workspace::Documents *_documents;
};

int run_language_server(
    ::lsp::io::Stream &stream, std::string_view serverName,
    const std::function<bool(pegium::SharedServices &)> &registerLanguageServices,
    const ServiceRequirements &serviceRequirements,
    pegium::SharedServices &services) {
  ::lsp::Connection connection(stream);

  pegium::initializeSharedServicesForLanguageServer(services, connection);
  if (!registerLanguageServices || !registerLanguageServices(services)) {
    std::cerr << "Failed to register " << serverName << " language services\n";
    return 1;
  }

  return pegium::startLanguageServer(services, connection,
                                     serviceRequirements);
}

} // namespace

void addConfigurationChangeHandler(
    ::lsp::MessageHandler &messageHandler,
    const pegium::SharedServices &sharedServices,
    std::function<void()> ensureInitialized) {
  messageHandler.add<::lsp::notifications::Workspace_DidChangeConfiguration>(
      [&sharedServices, ensureInitialized = std::move(ensureInitialized)](
          const ::lsp::DidChangeConfigurationParams &params) {
        if (ensureInitialized) {
          ensureInitialized();
        }
        sharedServices.workspace.configurationProvider->updateConfiguration(
            to_configuration_change_params(params));
      });
}

void addDiagnosticsHandler(::lsp::MessageHandler &messageHandler,
                           pegium::SharedServices &sharedServices,
                           utils::DisposableStore &disposables) {
  disposables.add(sharedServices.workspace.documentBuilder->onUpdate(
      [&messageHandler, documents = sharedServices.workspace.documents.get()](
          std::span<const workspace::DocumentId>,
          std::span<const workspace::DocumentId> deletedDocumentIds) {
        for (const auto documentId : deletedDocumentIds) {
          const auto uri = documents->getDocumentUri(documentId);
          if (uri.empty()) {
            continue;
          }
          publish_diagnostics(&messageHandler,
                              workspace::DocumentDiagnosticsSnapshot{
                                  .uri = uri,
                                  .text = {},
                                  .version = std::nullopt,
                                  .diagnostics = {},
                              });
        }
      }));

  disposables.add(sharedServices.workspace.documentBuilder->onDocumentPhase(
      workspace::DocumentState::Validated,
      [&messageHandler, textDocuments = sharedServices.lsp.textDocuments,
       documents = sharedServices.workspace.documents.get()](
          const std::shared_ptr<workspace::Document> &document,
          utils::CancellationToken) {
        assert(document != nullptr);
        if (textDocuments != nullptr) {
          if (const auto latest = textDocuments->getNormalized(document->uri);
              latest != nullptr &&
              latest->version() != document->textDocument().version()) {
            return;
          }
        }
        const CrossFileTextDocumentProvider crossFileProvider(
            textDocuments.get(), documents);
        publish_diagnostics(&messageHandler,
                            workspace::DocumentDiagnosticsSnapshot{
                                .uri = document->uri,
                                .text = std::string(document->textDocument().getText()),
                                .version = document->textDocument().version(),
                                .diagnostics = document->diagnostics,
                            },
                            &crossFileProvider);
      }));
}

void addDocumentUpdateHandler(::lsp::MessageHandler &messageHandler,
                              pegium::SharedServices &sharedServices,
                              std::function<void()> ensureInitialized,
                              utils::DisposableStore &disposables) {
  const auto documents = sharedServices.lsp.textDocuments;
  assert(documents != nullptr);
  assert(sharedServices.lsp.documentUpdateHandler != nullptr);
  auto &handler = *sharedServices.lsp.documentUpdateHandler;
  const bool supportsDidSave = handler.supportsDidSaveDocument();
  const bool supportsWillSave = handler.supportsWillSaveDocument();
  const bool supportsWillSaveWaitUntil =
      handler.supportsWillSaveDocumentWaitUntil();

  disposables.add(documents->onDidOpen(
      [&handler](const workspace::TextDocumentChangeEvent &event) {
        handler.didOpenDocument(event);
      }));
  disposables.add(documents->onDidChangeContent(
      [&handler](const workspace::TextDocumentChangeEvent &event) {
        handler.didChangeContent(event);
      }));
  if (supportsDidSave) {
    disposables.add(documents->onDidSave(
        [&handler](const workspace::TextDocumentChangeEvent &event) {
          handler.didSaveDocument(event);
        }));
  }
  disposables.add(documents->onDidClose(
      [&handler](const workspace::TextDocumentChangeEvent &event) {
        handler.didCloseDocument(event);
      }));
  if (supportsWillSave) {
    disposables.add(documents->onWillSave(
        [&handler](const workspace::TextDocumentWillSaveEvent &event) {
          handler.willSaveDocument(TextDocumentWillSaveEvent{
              .document = event.document,
              .reason = to_lsp_save_reason(event.reason),
          });
        }));
  }
  if (supportsWillSaveWaitUntil) {
    disposables.add(documents->onWillSaveWaitUntil(
        [&handler](const workspace::TextDocumentWillSaveEvent &event) {
          std::vector<workspace::TextEdit> edits;
          for (const auto &edit :
               handler.willSaveDocumentWaitUntil(TextDocumentWillSaveEvent{
                   .document = event.document,
                   .reason = to_lsp_save_reason(event.reason),
               })) {
            edits.push_back(to_workspace_text_edit(edit));
          }
          return edits;
        }));
  }

  disposables.add(documents->listen(messageHandler, ensureInitialized));

  messageHandler.add<::lsp::notifications::Workspace_DidChangeWatchedFiles>(
      [&handler, ensureInitialized = std::move(ensureInitialized)](
          const ::lsp::DidChangeWatchedFilesParams &params) {
        if (ensureInitialized) {
          ensureInitialized();
        }
        handler.didChangeWatchedFiles(params);
      });
}

void addFileOperationHandler(::lsp::MessageHandler &messageHandler,
                             FileOperationHandler &fileOperationHandler,
                             const std::function<void()> &ensureInitialized) {
  const auto options = fileOperationHandler.fileOperationOptions();

  if (options.didCreate.has_value()) {
    messageHandler.add<::lsp::notifications::Workspace_DidCreateFiles>(
        [&fileOperationHandler, ensureInitialized = ensureInitialized](
            const ::lsp::CreateFilesParams &params) {
          if (ensureInitialized) {
            ensureInitialized();
          }
          fileOperationHandler.didCreateFiles(params);
        });
  }

  if (options.didRename.has_value()) {
    messageHandler.add<::lsp::notifications::Workspace_DidRenameFiles>(
        [&fileOperationHandler, ensureInitialized = ensureInitialized](
            const ::lsp::RenameFilesParams &params) {
          if (ensureInitialized) {
            ensureInitialized();
          }
          fileOperationHandler.didRenameFiles(params);
        });
  }

  if (options.didDelete.has_value()) {
    messageHandler.add<::lsp::notifications::Workspace_DidDeleteFiles>(
        [&fileOperationHandler, ensureInitialized = ensureInitialized](
            const ::lsp::DeleteFilesParams &params) {
          if (ensureInitialized) {
            ensureInitialized();
          }
          fileOperationHandler.didDeleteFiles(params);
        });
  }

  if (options.willCreate.has_value()) {
    messageHandler.add<::lsp::requests::Workspace_WillCreateFiles>(
        [&fileOperationHandler, ensureInitialized = ensureInitialized](
            const ::lsp::CreateFilesParams &params) {
          if (ensureInitialized) {
            ensureInitialized();
          }
          const auto edit = fileOperationHandler.willCreateFiles(params);
          if (!edit.has_value()) {
            return ::lsp::Workspace_WillCreateFilesResult{};
          }
          return ::lsp::Workspace_WillCreateFilesResult{*edit};
        });
  }

  if (options.willRename.has_value()) {
    messageHandler.add<::lsp::requests::Workspace_WillRenameFiles>(
        [&fileOperationHandler, ensureInitialized = ensureInitialized](
            const ::lsp::RenameFilesParams &params) {
          if (ensureInitialized) {
            ensureInitialized();
          }
          const auto edit = fileOperationHandler.willRenameFiles(params);
          if (!edit.has_value()) {
            return ::lsp::Workspace_WillRenameFilesResult{};
          }
          return ::lsp::Workspace_WillRenameFilesResult{*edit};
        });
  }

  if (options.willDelete.has_value()) {
    messageHandler.add<::lsp::requests::Workspace_WillDeleteFiles>(
        [&fileOperationHandler, ensureInitialized = ensureInitialized](
            const ::lsp::DeleteFilesParams &params) {
          if (ensureInitialized) {
            ensureInitialized();
          }
          const auto edit = fileOperationHandler.willDeleteFiles(params);
          if (!edit.has_value()) {
            return ::lsp::Workspace_WillDeleteFilesResult{};
          }
          return ::lsp::Workspace_WillDeleteFilesResult{*edit};
        });
  }
}

int startLanguageServer(pegium::SharedServices &sharedServices,
                        ::lsp::Connection &connection,
                        const ServiceRequirements &serviceRequirements) {
  if (sharedServices.lsp.languageServer == nullptr) {
    return 1;
  }

  ignore_sigpipe_process_wide();

  LanguageServerRuntimeState runtimeState;
  runtimeState.reset();
  LanguageServerHandlerContext handlerContext(
      *sharedServices.lsp.languageServer, sharedServices, runtimeState);

  utils::DisposableStore runtimeDisposables;

  // Declared last so it is destroyed first: ~MessageHandler drains and joins
  // its worker thread pool (running any in-flight request task to completion)
  // while handlerContext and runtimeState — captured by reference by those
  // tasks — are still alive. A request still executing on a worker at teardown
  // would otherwise call back into already-destroyed state (use-after-free).
  ::lsp::MessageHandler messageHandler(connection);
  sharedServices.lsp.languageClient = make_message_handler_language_client(
      messageHandler, *sharedServices.observabilitySink);

  addConfigurationChangeHandler(
      messageHandler, sharedServices,
      [&runtimeState]() { ensure_initialized(runtimeState); });
  addDiagnosticsHandler(messageHandler, sharedServices, runtimeDisposables);
  addDocumentUpdateHandler(
      messageHandler, sharedServices,
      [&runtimeState]() { ensure_initialized(runtimeState); },
      runtimeDisposables);
  if (sharedServices.lsp.fileOperationHandler != nullptr) {
    addFileOperationHandler(
        messageHandler, *sharedServices.lsp.fileOperationHandler,
        [&runtimeState]() { ensure_initialized(runtimeState); });
  }
  addLanguageServerRequestHandlers(handlerContext, serviceRequirements,
                                   messageHandler);

  while (!runtimeState.exitRequested()) {
    try {
      messageHandler.processIncomingMessages();
    } catch (const ::lsp::ConnectionError &) {
      break;
    } catch (const ::lsp::io::Error &) {
      break;
    } catch (const std::exception &) {
      break;
    } catch (...) {
      break;
    }
  }

  const bool shutdownRequested = runtimeState.shutdownRequested();
  if (shutdownRequested) {
    runtimeState.waitForPendingRequests();
  }

  // Quiesce in-flight document-update dispatches before tearing down the
  // diagnostics listeners and the messageHandler they capture by reference. The
  // document builder invokes those listeners from the update-dispatch threads
  // off a snapshot copy, so disposing them below does not stop an emission
  // already in flight; a build still running when the connection drops would
  // otherwise publish into a messageHandler that is about to be destroyed
  // (use-after-free). waitForPendingRequests only drains the request worker
  // pool, not these builder dispatches.
  if (sharedServices.lsp.documentUpdateHandler != nullptr) {
    sharedServices.lsp.documentUpdateHandler->quiesce();
  }

  runtimeDisposables.dispose();
  sharedServices.lsp.languageClient.reset();
  runtimeState.reset();
  return shutdownRequested ? 0 : 1;
}

namespace detail {

int runLanguageServerMainOn(
    int argc, char **argv, std::string_view serverName,
    const std::function<bool(pegium::SharedServices &)> &registerLanguageServices,
    const ServiceRequirements &serviceRequirements,
    pegium::SharedServices &sharedServices) {
  try {
    if (const auto port = parse_port_arg(argc, argv); port.has_value()) {
      std::cerr << serverName << " listening on "
                << ::lsp::io::Socket::Localhost << ':' << *port << '\n';
      auto listener = ::lsp::io::SocketListener(*port, 1);
      auto socket = listener.listen();
      std::cerr << serverName << " accepted socket connection\n";
      return run_language_server(socket, serverName, registerLanguageServices,
                                 serviceRequirements, sharedServices);
    }

    auto &stream = ::lsp::io::standardIO();
    return run_language_server(stream, serverName, registerLanguageServices,
                               serviceRequirements, sharedServices);
  } catch (const std::exception &error) {
    std::cerr << serverName << " fatal error: " << error.what() << '\n';
    return 2;
  }
}

} // namespace detail

} // namespace pegium
