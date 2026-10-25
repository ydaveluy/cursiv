#include "cursiv/lsp/runtime/LanguageServerRuntime.hpp"

#include <lsp/error.h>
#include <lsp/io/standard_io.h>
#include <lsp/io/stream.h>
#include <lsp/json/json.h>
#include <lsp/request_result.h>
#include <lsp/server_endpoint.h>
#include <lsp/task_function.h>
#include <signal.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "cursiv/core/execution/TaskScheduler.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"
#include "cursiv/lsp/runtime/LanguageServer.hpp"
#include "cursiv/lsp/runtime/ServerEndpointLanguageClient.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"
#include "cursiv/lsp/workspace/ConfigurationProvider.hpp"
#include "cursiv/lsp/workspace/FileOperationHandler.hpp"
#include "cursiv/mcp/McpServer.hpp"
#include "cursiv/mcp/McpService.hpp"
#include "lsp/types.h"

namespace cursiv {

namespace {

[[noreturn]] void throw_request_cancelled() {
  throw lsp::RequestError(lsp::MessageError::RequestCancelled,
                          "Request cancelled");
}

// A dead write must not kill the process: when the editor goes away
// mid-reply, the loop below wants the io error, not SIGPIPE. WASI has
// no signals to ignore -- a write to a closed pipe already returns the
// error this asks for.
void ignore_sigpipe_process_wide() {
#if !defined(_WIN32) && !defined(__wasi__)
  static const bool ignored = []() {
    std::signal(SIGPIPE, SIG_IGN);
    return true;
  }();
  (void)ignored;
#endif
}

// Declared before the endpoint and installed after it: the pool's threads
// write through this client, so it is removed only once they are joined.
class LanguageClientGuard {
 public:
  explicit LanguageClientGuard(SharedLspServices& shared) noexcept
      : _shared(shared) {}
  LanguageClientGuard(const LanguageClientGuard&) = delete;
  LanguageClientGuard& operator=(const LanguageClientGuard&) = delete;
  ~LanguageClientGuard() {
    if (_shared.configurationProvider != nullptr)
      _shared.configurationProvider->initialized(nullptr);
    _shared.languageClient = {};
  }

  void install(lsp::ServerEndpoint& endpoint,
               ServerEndpointLanguageClient::Defer defer) {
    _shared.languageClient = std::make_unique<ServerEndpointLanguageClient>(
        endpoint, std::move(defer));
  }

 private:
  SharedLspServices& _shared;
};

// The workspace writer: notifications run here in arrival order, so the read
// loop stays free for the cancellation, the shutdown and the exit a build may
// need. Queuing a document update cancels the write in flight past the scan.
class NotificationQueue {
 public:
  NotificationQueue(WorkspaceLock& lock, const WorkspaceManager* manager)
      : _lock(lock), _manager(manager) {
    _worker = std::jthread([this](const std::stop_token& stop) { run(stop); });
  }
  NotificationQueue(const NotificationQueue&) = delete;
  NotificationQueue& operator=(const NotificationQueue&) = delete;
  ~NotificationQueue() { stop(); }

  // What has been taken in, and what has been served: a request records the
  // first on the read loop, where the client's own order is still known, and
  // waits for the second before it reads anything.
  [[nodiscard]] std::uint64_t posted() const {
    const std::scoped_lock lock(_m);
    return _posted;
  }

  void wait_served(std::uint64_t upTo, const CancellationToken& token) {
    std::unique_lock lock(_m);
    // Requests normally find their predecessors served. Only a pending wait
    // needs to register with the cancellation token.
    if (!_stopped && _served < upTo)
      _cv.wait(lock, token, [&]() { return _stopped || _served >= upTo; });
    if (_stopped || token.stop_requested()) throw_request_cancelled();
  }

  void post(std::function<void()> task, bool supersedes) {
    // Cancel before publishing: once queued, this task may already be the
    // latest writer, even before notify_all. Cancellation runs callbacks, so
    // it must also stay outside the queue mutex.
    if (supersedes && scanned()) _lock.cancelWrite();
    {
      const std::scoped_lock lock(_m);
      if (_stopped) return;
      _tasks.push_back(std::move(task));
      ++_posted;
    }
    _cv.notify_all();
  }

  // Returns once every posted task has run.
  void drain() {
    std::unique_lock lock(_m);
    _cv.wait(lock, [this]() { return _tasks.empty() && !_running; });
  }

  // Cuts the task in flight at its next check and drops the rest.
  void stop() {
    {
      const std::scoped_lock lock(_m);
      if (_stopped) return;
      _stopped = true;
      _tasks.clear();
    }
    _worker.request_stop();
    _cv.notify_all();
    _lock.cancelWrite();
    if (_worker.joinable()) _worker.join();
  }

 private:
  [[nodiscard]] bool scanned() const {
    if (_manager == nullptr) return true;
    return _manager->ready().wait_for(std::chrono::seconds(0)) ==
           std::future_status::ready;
  }

  void run(const std::stop_token& stop) {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(_m);
        _cv.wait(lock,
                 [&]() { return stop.stop_requested() || !_tasks.empty(); });
        if (_tasks.empty()) return;
        task = std::move(_tasks.front());
        _tasks.pop_front();
        _running = true;
      }
      try {
        task();
      } catch (...) {
        // A notification has no reply to carry it.
      }
      {
        const std::scoped_lock lock(_m);
        _running = false;
        ++_served;
      }
      _cv.notify_all();
    }
  }

  WorkspaceLock& _lock;
  const WorkspaceManager* _manager;
  mutable std::mutex _m;
  std::uint64_t _posted = 0;
  std::uint64_t _served = 0;
  std::condition_variable_any _cv;
  std::deque<std::function<void()>> _tasks;
  bool _running = false;
  bool _stopped = false;
  std::jthread _worker;
};

// The requests handed to the endpoint's pool and not yet over: a clean
// shutdown waits for them instead of letting the endpoint stop them.
class PendingRequests {
 public:
  void enter() {
    const std::scoped_lock lock(_m);
    ++_count;
  }

  void leave() {
    {
      const std::scoped_lock lock(_m);
      --_count;
    }
    _cv.notify_all();
  }

  void drain() {
    std::unique_lock lock(_m);
    _cv.wait(lock, [this]() { return _count == 0; });
  }

 private:
  std::mutex _m;
  std::condition_variable _cv;
  std::size_t _count = 0;
};

// However the body ends, it leaves.
struct LeaveOnExit {
  PendingRequests& requests;
  ~LeaveOnExit() { requests.leave(); }
};

struct Session {
  lsp::ServerEndpoint& endpoint;
  LanguageServer& server;
  NotificationQueue& queue;
  PendingRequests& pending;
  // False when the host gives no threads: every body runs on the read loop.
  bool pooled;
  bool shutdownRequested = false;
  bool definitionLinks = false;
  bool declarationLinks = false;
  bool typeDefinitionLinks = false;
  bool implementationLinks = false;
};

// Stop the producer before destroying the endpoint: its builds publish
// through the client, which writes through the endpoint.
struct StopNotifications {
  NotificationQueue& queue;
  ~StopNotifications() { queue.stop(); }
};

// The prologue runs on the read loop; the body is queued. `supersedes` is a
// document update, which cancels the write in flight.
template <class Call>
auto notification(Session& session, bool supersedes, Call call) {
  return [&queue = session.queue, supersedes,
          call = std::move(call)](auto&& params) {
    // Said here and not in the body: the writer takes the update later, and
    // a request dispatched behind it would be answered from the rung the
    // build before it published. A client's messages are served in the order
    // it sent them, and this is where that order is still known.
    queue.post([call, params = std::forward<decltype(params)>(
                          params)]() mutable { call(params); },
               supersedes);
  };
}

// A cancelled request answers `RequestCancelled` however the cancellation
// surfaced: a half-computed result is never dressed up as an answer.
template <class Result, class Call, class Params>
Result serve(NotificationQueue& queue, std::uint64_t behind,
             const CancellationToken& token, Call& call, Params&& params) {
  queue.wait_served(behind, token);
  Result result;
  try {
    result = call(std::move(params), token);
  } catch (const OperationCancelled&) {
    throw_request_cancelled();
  }
  if (token.stop_requested()) throw_request_cancelled();
  return result;
}

// Every request leaves the read loop -- lifecycle refusals are the endpoint's
// hook, before this -- for the endpoint's pool, so the loop goes read the next
// message, which may be the `$/cancelRequest` that stops this one's token.
template <class Call>
auto request(Session& session, Call call) {
  return [&session, call = std::move(call)](auto&& params) {
    using Params = std::remove_cvref_t<decltype(params)>;
    using Result =
        std::invoke_result_t<Call&, Params&&, const CancellationToken&>;
    // The notifications this request stands behind, counted here because
    // here is where the client's order still is: the body runs later, and
    // a document update served after it would answer the reading before.
    const std::uint64_t behind = session.queue.posted();
    if (!session.pooled)
      return lsp::RequestResult<Result>(
          serve<Result>(session.queue, behind, kNoCancel, call, params));
    auto task = lsp::TaskFunction<Result>(
        [&queue = session.queue, &requests = session.pending,
         call, behind, token = lsp::RequestContext::get().stopToken(),
         params = std::forward<decltype(params)>(params)]() mutable {
          const LeaveOnExit leave{requests};
          return serve<Result>(queue, behind, token, call, std::move(params));
        });
    session.pending.enter();
    return lsp::RequestResult<Result>(std::move(task));
  };
}

void add_lifecycle_handlers(Session& session, SharedCoreServices& shared) {
  lsp::ServerEndpoint& endpoint = session.endpoint;
  LanguageServer& server = session.server;
  endpoint.onInitialize([&session, &server](lsp::InitializeParams&& params) {
    if (params.capabilities.textDocument.has_value()) {
      const auto& text = *params.capabilities.textDocument;
      session.definitionLinks =
          text.definition && text.definition->linkSupport.value_or(false);
      session.declarationLinks =
          text.declaration && text.declaration->linkSupport.value_or(false);
      session.typeDefinitionLinks =
          text.typeDefinition && text.typeDefinition->linkSupport.value_or(false);
      session.implementationLinks =
          text.implementation && text.implementation->linkSupport.value_or(false);
    }
    return server.initialize(params);
  });

  // The workspace scan, the settings pull and the file-watcher
  // registration all hang off this one notification, which is why it
  // is a hook and not a step of `initialize`.
  endpoint.onInitialized(notification(
      session, false,
      [&server](const lsp::InitializedParams&) { server.initialized(); }));

  // Every queued notification runs, then an empty build publishes every
  // rung a cut build never did, so each waiter learns its answer.
  endpoint.onShutdown([&session, &shared]() {
    session.shutdownRequested = true;
    session.queue.post(
        [&shared]() {
          shared.workspaceLock->write(
              [&](const CancellationToken& token) {
                shared.documentBuilder->build({}, {}, token);
              },
              WorkspaceLock::WriteMode::Durable);
        },
        false);
    session.queue.drain();
    return nullptr;
  });

  // Empty, but not optional: the endpoint's hook on it is what ends the loop.
  endpoint.onExit([] {});
}

lsp::TextDocumentDefinitionResult navigation_result(
    std::vector<lsp::LocationLink> links, bool linkSupport) {
  lsp::TextDocumentDefinitionResult result;
  if (linkSupport) {
    result = std::move(links);
  } else {
    std::vector<lsp::Location> locations;
    locations.reserve(links.size());
    for (auto& link : links)
      locations.push_back({std::move(link.targetUri), link.targetSelectionRange});
    result = std::move(locations);
  }
  return result;
}

void add_text_document_handlers(Session& session) {
  lsp::ServerEndpoint& endpoint = session.endpoint;
  LanguageServer& server = session.server;
  endpoint.onTextDocumentDidOpen(notification(
      session, true, [&server](const lsp::DidOpenTextDocumentParams& params) {
        server.didOpen(params);
      }));

  endpoint.onTextDocumentDidChange(notification(
      session, true, [&server](const lsp::DidChangeTextDocumentParams& params) {
        server.didChange(params);
      }));

  endpoint.onTextDocumentDidClose(notification(
      session, true, [&server](const lsp::DidCloseTextDocumentParams& params) {
        server.didClose(params);
      }));

  // The hierarchical arm of the answer: the flat `SymbolInformation` list is
  // the legacy one, and the outline this storey builds is a tree.
  endpoint.onTextDocumentDocumentSymbol(
      request(session, [&server](lsp::DocumentSymbolParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentDocumentSymbolResult result;
        result = server.documentSymbol(params, token);
        return result;
      }));

  // Keep the richer links only when the client supports them.
  endpoint.onTextDocumentDefinition(
      request(session, [&session, &server](lsp::DefinitionParams&& params,
                                 const CancellationToken& token) {
        return navigation_result(server.definition(params, token),
                                 session.definitionLinks);
      }));

  endpoint.onTextDocumentDeclaration(
      request(session, [&session, &server](lsp::DeclarationParams&& params,
                                 const CancellationToken& token) {
        return navigation_result(server.declaration(params, token),
                                 session.declarationLinks);
      }));

  endpoint.onTextDocumentTypeDefinition(
      request(session, [&session, &server](lsp::TypeDefinitionParams&& params,
                                 const CancellationToken& token) {
        return navigation_result(server.typeDefinition(params, token),
                                 session.typeDefinitionLinks);
      }));

  endpoint.onTextDocumentImplementation(
      request(session, [&session, &server](lsp::ImplementationParams&& params,
                                 const CancellationToken& token) {
        return navigation_result(server.implementation(params, token),
                                 session.implementationLinks);
      }));

  // The array arm, empty and not null: an empty list is "nothing names
  // this", while null reads as "the server could not answer".
  endpoint.onTextDocumentReferences(request(
      session,
      [&server](lsp::ReferenceParams&& params, const CancellationToken& token) {
        lsp::TextDocumentReferencesResult result;
        result = server.references(params, token);
        return result;
      }));

  endpoint.onTextDocumentDocumentHighlight(
      request(session, [&server](lsp::DocumentHighlightParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentDocumentHighlightResult result;
        result = server.documentHighlight(params, token);
        return result;
      }));

  // Null is the protocol's "this cannot be renamed": the editor shows
  // its own message. The other two arms -- a placeholder, a default --
  // would both promise a rename this server may refuse.
  endpoint.onTextDocumentPrepareRename(
      request(session, [&server](lsp::PrepareRenameParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentPrepareRenameResult result;
        if (const std::optional<lsp::Range> span =
                server.prepareRename(params, token))
          result = lsp::PrepareRenameResult(*span);
        return result;
      }));

  // Null again for a refused rename: a client with no edit applies
  // nothing, with no message worth an error's interruption.
  endpoint.onTextDocumentRename(request(
      session,
      [&server](lsp::RenameParams&& params, const CancellationToken& token) {
        lsp::TextDocumentRenameResult result;
        if (std::optional<lsp::WorkspaceEdit> edit =
                server.rename(params, token))
          result = std::move(*edit);
        return result;
      }));

  endpoint.onTextDocumentSemanticTokensFull(
      request(session, [&server](lsp::SemanticTokensParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSemanticTokensFullResult result;
        result = server.semanticTokensFull(params, token);
        return result;
      }));

  endpoint.onTextDocumentSemanticTokensRange(
      request(session, [&server](lsp::SemanticTokensRangeParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSemanticTokensRangeResult result;
        result = server.semanticTokensRange(params, token);
        return result;
      }));

  endpoint.onTextDocumentInlayHint(request(
      session,
      [&server](lsp::InlayHintParams&& params, const CancellationToken& token) {
        lsp::TextDocumentInlayHintResult result;
        result = server.inlayHint(params, token);
        return result;
      }));

  endpoint.onTextDocumentFoldingRange(
      request(session, [&server](lsp::FoldingRangeParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentFoldingRangeResult result;
        result = server.foldingRange(params, token);
        return result;
      }));

  endpoint.onTextDocumentSelectionRange(
      request(session, [&server](lsp::SelectionRangeParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSelectionRangeResult result;
        result = server.selectionRange(params, token);
        return result;
      }));

  // Null is the protocol's "nothing to show", and the default of the
  // result: a position on no documented word leaves it untouched.
  endpoint.onTextDocumentHover(request(
      session,
      [&server](lsp::HoverParams&& params, const CancellationToken& token) {
        lsp::TextDocumentHoverResult result;
        if (std::optional<lsp::Hover> hover = server.hover(params, token))
          result = std::move(*hover);
        return result;
      }));

  // The list form, not the bare array: `isIncomplete` matters here --
  // the frontier was computed at one anchor, and the next keystroke
  // must ask again rather than filter what it already has.
  endpoint.onTextDocumentCompletion(
      request(session, [&server](lsp::CompletionParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentCompletionResult result;
        result = server.completion(params, token);
        return result;
      }));

  // Null when there is no signature under the caret: the popup closes rather
  // than showing an empty frame.
  endpoint.onTextDocumentSignatureHelp(
      request(session, [&server](lsp::SignatureHelpParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSignatureHelpResult result;
        if (std::optional<lsp::SignatureHelp> help =
                server.signatureHelp(params, token))
          result = std::move(*help);
        return result;
      }));

  // Null for "nothing to offer", the protocol's own spelling: an empty array
  // would still draw a lightbulb on some clients.
  endpoint.onTextDocumentCodeAction(
      request(session, [&server](lsp::CodeActionParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentCodeActionResult result;
        std::vector<lsp::CodeAction> actions = server.codeAction(params, token);
        if (!actions.empty()) {
          lsp::Array<lsp::OneOf<lsp::Command, lsp::CodeAction>> encoded;
          encoded.reserve(actions.size());
          for (lsp::CodeAction& action : actions)
            encoded.emplace_back(std::move(action));
          result = std::move(encoded);
        }
        return result;
      }));

  // The three formatting arms answer empty as the array, not null: "no
  // edits" and "cannot format this" are the same thing to an editor,
  // and the empty list leaves the document alone.
  endpoint.onTextDocumentFormatting(
      request(session, [&server](lsp::DocumentFormattingParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentFormattingResult result;
        result = server.formatting(params, token);
        return result;
      }));

  endpoint.onTextDocumentRangeFormatting(
      request(session, [&server](lsp::DocumentRangeFormattingParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentRangeFormattingResult result;
        result = server.rangeFormatting(params, token);
        return result;
      }));

  endpoint.onTextDocumentOnTypeFormatting(
      request(session, [&server](lsp::DocumentOnTypeFormattingParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentOnTypeFormattingResult result;
        result = server.onTypeFormatting(params, token);
        return result;
      }));

  endpoint.onTextDocumentDocumentLink(
      request(session, [&server](lsp::DocumentLinkParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentDocumentLinkResult result;
        result = server.documentLink(params, token);
        return result;
      }));

  endpoint.onTextDocumentCodeLens(request(
      session,
      [&server](lsp::CodeLensParams&& params, const CancellationToken& token) {
        lsp::TextDocumentCodeLensResult result;
        result = server.codeLens(params, token);
        return result;
      }));

  // The two hierarchies, three arms each. The client walks the tree one level
  // at a time, so an expansion is its own request carrying the item it expands.
  endpoint.onTextDocumentPrepareCallHierarchy(
      request(session, [&server](lsp::CallHierarchyPrepareParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentPrepareCallHierarchyResult result;
        result = server.prepareCallHierarchy(params, token);
        return result;
      }));

  endpoint.onCallHierarchyIncomingCalls(
      request(session, [&server](lsp::CallHierarchyIncomingCallsParams&& params,
                                 const CancellationToken& token) {
        lsp::CallHierarchyIncomingCallsResult result;
        result = server.incomingCalls(params, token);
        return result;
      }));

  endpoint.onCallHierarchyOutgoingCalls(
      request(session, [&server](lsp::CallHierarchyOutgoingCallsParams&& params,
                                 const CancellationToken& token) {
        lsp::CallHierarchyOutgoingCallsResult result;
        result = server.outgoingCalls(params, token);
        return result;
      }));

  endpoint.onTextDocumentPrepareTypeHierarchy(
      request(session, [&server](lsp::TypeHierarchyPrepareParams&& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentPrepareTypeHierarchyResult result;
        result = server.prepareTypeHierarchy(params, token);
        return result;
      }));

  endpoint.onTypeHierarchySupertypes(
      request(session, [&server](lsp::TypeHierarchySupertypesParams&& params,
                                 const CancellationToken& token) {
        lsp::TypeHierarchySupertypesResult result;
        result = server.supertypes(params, token);
        return result;
      }));

  endpoint.onTypeHierarchySubtypes(
      request(session, [&server](lsp::TypeHierarchySubtypesParams&& params,
                                 const CancellationToken& token) {
        lsp::TypeHierarchySubtypesResult result;
        result = server.subtypes(params, token);
        return result;
      }));

  // An unknown URI answers empty text rather than an error: the request has
  // no null arm, and "no such document" is not a protocol fault.
  endpoint.onWorkspaceTextDocumentContent(
      request(session, [&server](lsp::TextDocumentContentParams&& params,
                                 const CancellationToken& token) {
        return server.textDocumentContent(params, token)
            .value_or(lsp::TextDocumentContentResult{});
      }));
}

// The two notifications about the workspace, not a document. Both
// arrive only because the server asked for them at `initialized` --
// watcher and settings alike -- so a client that refused either never
// gets here.
void add_workspace_handlers(Session& session) {
  lsp::ServerEndpoint& endpoint = session.endpoint;
  LanguageServer& server = session.server;
  endpoint.onWorkspaceDidChangeWatchedFiles(notification(
      session, true, [&server](const lsp::DidChangeWatchedFilesParams& params) {
        server.didChangeWatchedFiles(params);
      }));

  endpoint.onWorkspaceDidChangeConfiguration(
      notification(session, false,
                   [&server](const lsp::DidChangeConfigurationParams& params) {
                     server.didChangeConfiguration(params);
                   }));

  endpoint.onWorkspaceSymbol(
      request(session, [&server](lsp::WorkspaceSymbolParams&& params,
                                 const CancellationToken& token) {
        lsp::WorkspaceSymbolResult result;
        result = server.workspaceSymbol(params, token);
        return result;
      }));

  // Registered whether or not the provider resolves: the capability says
  // which, and a symbol nobody resolves goes back as it came.
  endpoint.onWorkspaceSymbolResolve(request(
      session,
      [&server](lsp::WorkspaceSymbol&& params, const CancellationToken& token) {
        return server.workspaceSymbolResolve(params, token);
      }));

  // Null is "no result", also the answer for an unknown name: a
  // command nobody registered is not worth an error across the wire.
  endpoint.onWorkspaceExecuteCommand(
      request(session, [&server](lsp::ExecuteCommandParams&& params,
                                 const CancellationToken& token) {
        lsp::WorkspaceExecuteCommandResult result;
        if (std::optional<lsp::LSPAny> answer =
                server.executeCommand(params, token))
          result = std::move(*answer);
        return result;
      }));
}

// The explorer's six, the only routes registered conditionally: the
// handler's options are the registration, so an arm nobody announced
// is one nothing would ever send.
void add_file_operation_handlers(Session& session,
                                 const SharedLspServices& shared) {
  lsp::ServerEndpoint& endpoint = session.endpoint;
  LanguageServer& server = session.server;
  const FileOperationHandler* files = shared.fileOperationHandler.get();
  if (files == nullptr) return;
  const lsp::FileOperationOptions& options = files->fileOperationOptions();

  if (options.didCreate.has_value())
    endpoint.onWorkspaceDidCreateFiles(notification(
        session, false, [&server](const lsp::CreateFilesParams& params) {
          server.didCreateFiles(params);
        }));

  if (options.didRename.has_value())
    endpoint.onWorkspaceDidRenameFiles(notification(
        session, false, [&server](const lsp::RenameFilesParams& params) {
          server.didRenameFiles(params);
        }));

  if (options.didDelete.has_value())
    endpoint.onWorkspaceDidDeleteFiles(notification(
        session, false, [&server](const lsp::DeleteFilesParams& params) {
          server.didDeleteFiles(params);
        }));

  // Null is "nothing to change": the client moves the files with no
  // edit in front -- the protocol's spelling for a handler with
  // nothing to say.
  if (options.willCreate.has_value())
    endpoint.onWorkspaceWillCreateFiles(
        request(session, [&server](lsp::CreateFilesParams&& params,
                                   const CancellationToken& token) {
          lsp::WorkspaceWillCreateFilesResult result;
          if (std::optional<lsp::WorkspaceEdit> edit =
                  server.willCreateFiles(params, token))
            result = std::move(*edit);
          return result;
        }));

  if (options.willRename.has_value())
    endpoint.onWorkspaceWillRenameFiles(
        request(session, [&server](lsp::RenameFilesParams&& params,
                                   const CancellationToken& token) {
          lsp::WorkspaceWillRenameFilesResult result;
          if (std::optional<lsp::WorkspaceEdit> edit =
                  server.willRenameFiles(params, token))
            result = std::move(*edit);
          return result;
        }));

  if (options.willDelete.has_value())
    endpoint.onWorkspaceWillDeleteFiles(
        request(session, [&server](lsp::DeleteFilesParams&& params,
                                   const CancellationToken& token) {
          lsp::WorkspaceWillDeleteFilesResult result;
          if (std::optional<lsp::WorkspaceEdit> edit =
                  server.willDeleteFiles(params, token))
            result = std::move(*edit);
          return result;
        }));
}

// MCP rides the LSP wire as one method: the params carry an MCP
// message whole, the result its response -- null for a notification.
// The host process bridges this to a standard MCP transport (no socket
// under WASI). A request like the others, so the read loop stays free.
void add_mcp_tunnel(Session& session, const McpService& service) {
  auto answer = request(
      session,
      [server = std::shared_ptr<const McpServer>(service.createServer())](
          lsp::json::Value&& params, const CancellationToken& token) {
        std::optional<lsp::json::Value> response =
            server->handle(std::move(params), token);
        return response.has_value() ? std::move(*response)
                                    : lsp::json::Value(nullptr);
      });
  session.endpoint.onCustomRequest(
      "cursiv/mcp", [&session, answer = std::move(answer)](
                        lsp::json::Value&& params) mutable {
        // A custom method skips the endpoint's hook; the tunnel asks it the
        // same question the generated handlers do.
        session.endpoint.preMethodCall<lsp::GenericRequest>(params);
        return answer(std::move(params));
      });
}

}  // namespace

int start_language_server(SharedLspServices& shared, lsp::io::Stream& stream) {
  ignore_sigpipe_process_wide();

  // Declared before the endpoint: the pool it joins as it dies still serves
  // requests that wait on the queue and leave `pending`.
  LanguageClientGuard clientGuard(shared);
  NotificationQueue queue(*shared.workspaceLock, shared.workspaceManager.get());
  PendingRequests pending;
  lsp::ServerEndpoint endpoint(stream);
  // Zero workers is the host saying it has no threads to give: every request
  // is then answered on the read loop. Otherwise half of them answer, which
  // the endpoint cannot tell on its own: WASI reports a single core.
  const std::size_t workers = TaskScheduler::defaultWorkerCount();
  if (workers != 0)
    endpoint.messageHandler().setMaxResponseThreads(
        static_cast<unsigned>(std::max<std::size_t>(1, (workers + 1) / 2)));
  Session session{endpoint, *shared.languageServer, queue, pending,
                  workers != 0};
  const StopNotifications stopNotifications{queue};
  clientGuard.install(endpoint, [&queue](std::function<void()> reply) {
    queue.post(std::move(reply), false);
  });

  add_lifecycle_handlers(session, shared);
  add_text_document_handlers(session);
  add_workspace_handlers(session);
  add_file_operation_handlers(session, shared);
  if (shared.mcpService != nullptr) add_mcp_tunnel(session, *shared.mcpService);

  try {
    endpoint.runMessageLoop();
  } catch (...) {
    // A stream that dies ends the session as `exit` does, minus `shutdown`.
  }

  // A clean shutdown drains: the client is still reading, so answers still
  // computing are waited for. Otherwise the endpoint stops them as it dies.
  if (session.shutdownRequested) pending.drain();

  // The exit code is the protocol's: a session the client shut down first ends
  // at 0, a stream that died under it does not.
  return session.shutdownRequested ? 0 : 1;
}

int run_language_server_stdio(SharedLspServices& shared) {
  return start_language_server(shared, lsp::io::standardIO());
}

}  // namespace cursiv
