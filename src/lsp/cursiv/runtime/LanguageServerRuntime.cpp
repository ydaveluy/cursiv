#include "cursiv/runtime/LanguageServerRuntime.hpp"

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
#include <exception>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cursiv/execution/TaskScheduler.hpp"
#include "cursiv/observability/ObservabilitySink.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/WorkspaceLock.hpp"
#include "cursiv/workspace/WorkspaceManager.hpp"
#include "cursiv/runtime/LanguageServer.hpp"
#include "cursiv/runtime/ServerEndpointLanguageClient.hpp"
#include "cursiv/services/SharedLspServices.hpp"
#include "cursiv/workspace/ConfigurationProvider.hpp"
#include "cursiv/workspace/FileOperationHandler.hpp"
#include "cursiv/McpServer.hpp"
#include "cursiv/McpService.hpp"
#include "cursiv/utils/Utf8.hpp"
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
#if defined(SIGPIPE) && !defined(__wasi__)
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
    _worker = std::jthread([this] { run(); });
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

  void run() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(_m);
        // `_stopped` is published under the mutex: a stop told only by a flag
        // set outside it could notify between this test and the wait, and the
        // worker would sleep through it while `stop` joins.
        _cv.wait(lock, [&]() { return _stopped || !_tasks.empty(); });
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

// A request's place among the pending ones, held by the task that answers it:
// given back however the task ends, run or dropped unrun by the endpoint.
class PendingSeat {
 public:
  explicit PendingSeat(PendingRequests& requests) : _requests(&requests) {
    requests.enter();
  }
  PendingSeat(PendingSeat&& other) noexcept
      : _requests(std::exchange(other._requests, nullptr)) {}
  PendingSeat(const PendingSeat&) = delete;
  PendingSeat& operator=(const PendingSeat&) = delete;
  PendingSeat& operator=(PendingSeat&&) = delete;
  ~PendingSeat() {
    if (_requests != nullptr) _requests->leave();
  }

 private:
  PendingRequests* _requests;
};

struct Session {
  lsp::ServerEndpoint& endpoint;
  LanguageServer& server;
  NotificationQueue& queue;
  PendingRequests& pending;
  // Read at each failure, never kept: a host may install another sink.
  SharedCoreServices& shared;
  // False when the host gives no threads: every body runs on the read loop.
  bool pooled;
  bool shutdownRequested = false;
  bool definitionLinks = false;
  bool declarationLinks = false;
  bool typeDefinitionLinks = false;
  bool implementationLinks = false;
  bool hierarchicalSymbols = false;
  bool codeActionLiterals = false;
};

// Stop the producer before destroying the endpoint: its builds publish
// through the client, which writes through the endpoint.
struct StopNotifications {
  NotificationQueue& queue;
  ~StopNotifications() { queue.stop(); }
};

// A notification has no reply to carry its failure: the observability sink
// is told instead, and the session goes on.
void say_notification_threw(SharedCoreServices& shared, std::string_view method,
                            const std::exception_ptr& failure) noexcept {
  try {
    std::string what = "unknown exception";
    try {
      std::rethrow_exception(failure);
    } catch (const std::exception& thrown) {
      what = thrown.what();
    } catch (...) {  // NOLINT(bugprone-empty-catch): `what` already says it
    }
    shared.observabilitySink->publish(
        {.severity = ObservationSeverity::Warning,
         .code = ObservationCode::NotificationThrew,
         .message = std::format("{} threw: {}", method, what),
         .uri = {},
         .languageId = {}});
  } catch (...) {  // NOLINT(bugprone-empty-catch): nothing left to tell it to
  }
}

// The prologue runs on the read loop; the body is queued. `supersedes` is a
// document update, which cancels the write in flight. `method` names it to the
// sink and outlives the session: a protocol name, or a custom handler's key.
template <class Call>
auto notification(Session& session, std::string_view method, bool supersedes,
                  Call call) {
  return [&queue = session.queue, &shared = session.shared, method, supersedes,
          call = std::move(call)](auto&& params) {
    // Said here and not in the body: the writer takes the update later, and
    // a request dispatched behind it would be answered from the rung the
    // build before it published. A client's messages are served in the order
    // it sent them, and this is where that order is still known.
    queue.post(
        [&shared, method, call,
         params = std::forward<decltype(params)>(params)]() mutable {
          try {
            call(params);
          } catch (const OperationCancelled&) {
            throw;
          } catch (...) {
            say_notification_threw(shared, method, std::current_exception());
          }
        },
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
    result = call(std::forward<Params>(params), token);
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
          serve<Result>(session.queue, behind, kNoCancel, call,
                        std::forward<decltype(params)>(params)));
    auto task = lsp::TaskFunction<Result>(
        [&queue = session.queue, seat = PendingSeat(session.pending), call,
         behind, token = lsp::RequestContext::get().stopToken(),
         params = std::forward<decltype(params)>(params)]() mutable {
          return serve<Result>(queue, behind, token, call, std::move(params));
        });
    return lsp::RequestResult<Result>(std::move(task));
  };
}

void add_lifecycle_handlers(Session& session, SharedCoreServices& shared) {
  lsp::ServerEndpoint& endpoint = session.endpoint;
  LanguageServer& server = session.server;
  endpoint.onInitialize([&session,
                         &server](const lsp::InitializeParams& params) {
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
      session.hierarchicalSymbols =
          text.documentSymbol &&
          text.documentSymbol->hierarchicalDocumentSymbolSupport.value_or(false);
      session.codeActionLiterals =
          text.codeAction && text.codeAction->codeActionLiteralSupport.has_value();
    }
    return server.initialize(params);
  });

  // The workspace scan, the settings pull and the file-watcher
  // registration all hang off this one notification, which is why it
  // is a hook and not a step of `initialize`.
  endpoint.onInitialized(notification(
      session, lsp::notifications::Initialized::Method, false,
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
  endpoint.onExit([] { /* the hook alone ends the loop */ });
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
      locations.emplace_back(std::move(link.targetUri),
                             link.targetSelectionRange);
    result = std::move(locations);
  }
  return result;
}

// The outline as a flat list, each parent before its children. A symbol keeps
// its whole range and names its parent in `containerName`. A stack, not
// recursion: an outline is as deep as the document nests.
std::vector<lsp::SymbolInformation> flat_symbols(
    const lsp::DocumentUri& uri, std::vector<lsp::DocumentSymbol> symbols) {
  constexpr auto kTopLevel = static_cast<std::size_t>(-1);
  struct Pending {
    lsp::DocumentSymbol* symbol;
    std::size_t parent;
  };
  std::vector<Pending> pending;
  for (auto it = symbols.rbegin(); it != symbols.rend(); ++it)
    pending.push_back({std::to_address(it), kTopLevel});
  std::vector<lsp::SymbolInformation> flat;
  while (!pending.empty()) {
    const Pending next = pending.back();
    pending.pop_back();
    lsp::DocumentSymbol& symbol = *next.symbol;
    lsp::SymbolInformation info;
    info.name = std::move(symbol.name);
    info.kind = symbol.kind;
    info.tags = std::move(symbol.tags);
    info.deprecated = symbol.deprecated;
    if (next.parent != kTopLevel) info.containerName = flat[next.parent].name;
    info.location = {uri, symbol.range};
    flat.push_back(std::move(info));
    if (symbol.children.has_value())
      for (auto it = symbol.children->rbegin(); it != symbol.children->rend();
           ++it)
        pending.push_back({std::to_address(it), flat.size() - 1});
  }
  return flat;
}

void add_text_document_handlers(Session& session) {
  lsp::ServerEndpoint& endpoint = session.endpoint;
  LanguageServer& server = session.server;
  endpoint.onTextDocumentDidOpen(notification(
      session, lsp::notifications::TextDocumentDidOpen::Method, true,
      [&server](const lsp::DidOpenTextDocumentParams& params) {
        server.didOpen(params);
      }));

  endpoint.onTextDocumentDidChange(notification(
      session, lsp::notifications::TextDocumentDidChange::Method, true,
      [&server](const lsp::DidChangeTextDocumentParams& params) {
        server.didChange(params);
      }));

  endpoint.onTextDocumentDidClose(notification(
      session, lsp::notifications::TextDocumentDidClose::Method, true,
      [&server](const lsp::DidCloseTextDocumentParams& params) {
        server.didClose(params);
      }));

  // The hierarchical arm of the answer for a client that reads it: the
  // outline this storey builds is a tree. Any other client gets the flat
  // `SymbolInformation` list.
  endpoint.onTextDocumentDocumentSymbol(request(
      session, [&session, &server](const lsp::DocumentSymbolParams& params,
                                   const CancellationToken& token) {
        lsp::TextDocumentDocumentSymbolResult result;
        std::vector<lsp::DocumentSymbol> symbols =
            server.documentSymbol(params, token);
        if (session.hierarchicalSymbols)
          result = std::move(symbols);
        else
          result = flat_symbols(params.textDocument.uri, std::move(symbols));
        return result;
      }));

  // Keep the richer links only when the client supports them.
  endpoint.onTextDocumentDefinition(
      request(session, [&session, &server](const lsp::DefinitionParams& params,
                                           const CancellationToken& token) {
        return navigation_result(server.definition(params, token),
                                 session.definitionLinks);
      }));

  endpoint.onTextDocumentDeclaration(
      request(session, [&session, &server](const lsp::DeclarationParams& params,
                                           const CancellationToken& token) {
        return navigation_result(server.declaration(params, token),
                                 session.declarationLinks);
      }));

  endpoint.onTextDocumentTypeDefinition(request(
      session, [&session, &server](const lsp::TypeDefinitionParams& params,
                                   const CancellationToken& token) {
        return navigation_result(server.typeDefinition(params, token),
                                 session.typeDefinitionLinks);
      }));

  endpoint.onTextDocumentImplementation(request(
      session, [&session, &server](const lsp::ImplementationParams& params,
                                   const CancellationToken& token) {
        return navigation_result(server.implementation(params, token),
                                 session.implementationLinks);
      }));

  // The array arm, empty and not null: an empty list is "nothing names
  // this", while null reads as "the server could not answer".
  endpoint.onTextDocumentReferences(
      request(session, [&server](const lsp::ReferenceParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentReferencesResult result;
        result = server.references(params, token);
        return result;
      }));

  endpoint.onTextDocumentDocumentHighlight(
      request(session, [&server](const lsp::DocumentHighlightParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentDocumentHighlightResult result;
        result = server.documentHighlight(params, token);
        return result;
      }));

  // Null is the protocol's "this cannot be renamed": the editor shows
  // its own message. The other two arms -- a placeholder, a default --
  // would both promise a rename this server may refuse.
  endpoint.onTextDocumentPrepareRename(
      request(session, [&server](const lsp::PrepareRenameParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentPrepareRenameResult result;
        if (const std::optional<lsp::Range> span =
                server.prepareRename(params, token))
          result = lsp::PrepareRenameResult(*span);
        return result;
      }));

  // Null again for a refused rename: a client with no edit applies
  // nothing, with no message worth an error's interruption.
  endpoint.onTextDocumentRename(
      request(session, [&server](const lsp::RenameParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentRenameResult result;
        if (std::optional<lsp::WorkspaceEdit> edit =
                server.rename(params, token))
          result = std::move(*edit);
        return result;
      }));

  endpoint.onTextDocumentSemanticTokensFull(
      request(session, [&server](const lsp::SemanticTokensParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSemanticTokensFullResult result;
        result = server.semanticTokensFull(params, token);
        return result;
      }));

  endpoint.onTextDocumentSemanticTokensRange(
      request(session, [&server](const lsp::SemanticTokensRangeParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSemanticTokensRangeResult result;
        result = server.semanticTokensRange(params, token);
        return result;
      }));

  endpoint.onTextDocumentInlayHint(
      request(session, [&server](const lsp::InlayHintParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentInlayHintResult result;
        result = server.inlayHint(params, token);
        return result;
      }));

  endpoint.onTextDocumentFoldingRange(
      request(session, [&server](const lsp::FoldingRangeParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentFoldingRangeResult result;
        result = server.foldingRange(params, token);
        return result;
      }));

  endpoint.onTextDocumentSelectionRange(
      request(session, [&server](const lsp::SelectionRangeParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSelectionRangeResult result;
        result = server.selectionRange(params, token);
        return result;
      }));

  // Null is the protocol's "nothing to show", and the default of the
  // result: a position on no documented word leaves it untouched.
  endpoint.onTextDocumentHover(
      request(session, [&server](const lsp::HoverParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentHoverResult result;
        if (std::optional<lsp::Hover> hover = server.hover(params, token))
          result = std::move(*hover);
        return result;
      }));

  // The list form, not the bare array: `isIncomplete` matters here --
  // the frontier was computed at one anchor, and the next keystroke
  // must ask again rather than filter what it already has.
  endpoint.onTextDocumentCompletion(
      request(session, [&server](const lsp::CompletionParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentCompletionResult result;
        result = server.completion(params, token);
        return result;
      }));

  // Null when there is no signature under the caret: the popup closes rather
  // than showing an empty frame.
  endpoint.onTextDocumentSignatureHelp(
      request(session, [&server](const lsp::SignatureHelpParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentSignatureHelpResult result;
        if (std::optional<lsp::SignatureHelp> help =
                server.signatureHelp(params, token))
          result = std::move(*help);
        return result;
      }));

  // Null for "nothing to offer", the protocol's own spelling: an empty array
  // would still draw a lightbulb on some clients. A client without code action
  // literals reads commands only: an action that is just a command goes as
  // that command, and one with an edit or marked disabled is left out.
  endpoint.onTextDocumentCodeAction(
      request(session, [&session, &server](const lsp::CodeActionParams& params,
                                           const CancellationToken& token) {
        lsp::TextDocumentCodeActionResult result;
        std::vector<lsp::CodeAction> actions = server.codeAction(params, token);
        lsp::Array<lsp::OneOf<lsp::Command, lsp::CodeAction>> encoded;
        encoded.reserve(actions.size());
        for (lsp::CodeAction& action : actions) {
          if (session.codeActionLiterals)
            encoded.emplace_back(std::move(action));
          else if (action.command && !action.edit && !action.disabled)
            encoded.emplace_back(std::move(*action.command));
        }
        if (!encoded.empty()) result = std::move(encoded);
        return result;
      }));

  // The three formatting arms answer empty as the array, not null: "no
  // edits" and "cannot format this" are the same thing to an editor,
  // and the empty list leaves the document alone.
  endpoint.onTextDocumentFormatting(
      request(session, [&server](const lsp::DocumentFormattingParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentFormattingResult result;
        result = server.formatting(params, token);
        return result;
      }));

  endpoint.onTextDocumentRangeFormatting(request(
      session, [&server](const lsp::DocumentRangeFormattingParams& params,
                         const CancellationToken& token) {
        lsp::TextDocumentRangeFormattingResult result;
        result = server.rangeFormatting(params, token);
        return result;
      }));

  endpoint.onTextDocumentOnTypeFormatting(request(
      session, [&server](const lsp::DocumentOnTypeFormattingParams& params,
                         const CancellationToken& token) {
        lsp::TextDocumentOnTypeFormattingResult result;
        result = server.onTypeFormatting(params, token);
        return result;
      }));

  endpoint.onTextDocumentDocumentLink(
      request(session, [&server](const lsp::DocumentLinkParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentDocumentLinkResult result;
        result = server.documentLink(params, token);
        return result;
      }));

  endpoint.onTextDocumentCodeLens(
      request(session, [&server](const lsp::CodeLensParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentCodeLensResult result;
        result = server.codeLens(params, token);
        return result;
      }));

  // The two hierarchies, three arms each. The client walks the tree one level
  // at a time, so an expansion is its own request carrying the item it expands.
  endpoint.onTextDocumentPrepareCallHierarchy(
      request(session, [&server](const lsp::CallHierarchyPrepareParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentPrepareCallHierarchyResult result;
        result = server.prepareCallHierarchy(params, token);
        return result;
      }));

  endpoint.onCallHierarchyIncomingCalls(request(
      session, [&server](const lsp::CallHierarchyIncomingCallsParams& params,
                         const CancellationToken& token) {
        lsp::CallHierarchyIncomingCallsResult result;
        result = server.incomingCalls(params, token);
        return result;
      }));

  endpoint.onCallHierarchyOutgoingCalls(request(
      session, [&server](const lsp::CallHierarchyOutgoingCallsParams& params,
                         const CancellationToken& token) {
        lsp::CallHierarchyOutgoingCallsResult result;
        result = server.outgoingCalls(params, token);
        return result;
      }));

  endpoint.onTextDocumentPrepareTypeHierarchy(
      request(session, [&server](const lsp::TypeHierarchyPrepareParams& params,
                                 const CancellationToken& token) {
        lsp::TextDocumentPrepareTypeHierarchyResult result;
        result = server.prepareTypeHierarchy(params, token);
        return result;
      }));

  endpoint.onTypeHierarchySupertypes(request(
      session, [&server](const lsp::TypeHierarchySupertypesParams& params,
                         const CancellationToken& token) {
        lsp::TypeHierarchySupertypesResult result;
        result = server.supertypes(params, token);
        return result;
      }));

  endpoint.onTypeHierarchySubtypes(
      request(session, [&server](const lsp::TypeHierarchySubtypesParams& params,
                                 const CancellationToken& token) {
        lsp::TypeHierarchySubtypesResult result;
        result = server.subtypes(params, token);
        return result;
      }));

  // An unknown URI answers empty text rather than an error: the request has
  // no null arm, and "no such document" is not a protocol fault.
  endpoint.onWorkspaceTextDocumentContent(
      request(session, [&server](const lsp::TextDocumentContentParams& params,
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
      session, lsp::notifications::WorkspaceDidChangeWatchedFiles::Method, true,
      [&server](const lsp::DidChangeWatchedFilesParams& params) {
        server.didChangeWatchedFiles(params);
      }));

  endpoint.onWorkspaceDidChangeConfiguration(notification(
      session, lsp::notifications::WorkspaceDidChangeConfiguration::Method,
      false, [&server](const lsp::DidChangeConfigurationParams& params) {
        server.didChangeConfiguration(params);
      }));

  endpoint.onWorkspaceDidChangeWorkspaceFolders(notification(
      session,
      std::string(
          lsp::notifications::WorkspaceDidChangeWorkspaceFolders::Method),
      false, [&server](const lsp::DidChangeWorkspaceFoldersParams& params) {
        server.didChangeWorkspaceFolders(params);
      }));

  endpoint.onWorkspaceSymbol(
      request(session, [&server](const lsp::WorkspaceSymbolParams& params,
                                 const CancellationToken& token) {
        lsp::WorkspaceSymbolResult result;
        result = server.workspaceSymbol(params, token);
        return result;
      }));

  // Registered whether or not the provider resolves: the capability says
  // which, and a symbol nobody resolves goes back as it came.
  endpoint.onWorkspaceSymbolResolve(
      request(session, [&server](const lsp::WorkspaceSymbol& params,
                                 const CancellationToken& token) {
        return server.workspaceSymbolResolve(params, token);
      }));

  // Null is "no result", also the answer for an unknown name: a
  // command nobody registered is not worth an error across the wire.
  endpoint.onWorkspaceExecuteCommand(
      request(session, [&server](const lsp::ExecuteCommandParams& params,
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
        session, lsp::notifications::WorkspaceDidCreateFiles::Method, false,
        [&server](const lsp::CreateFilesParams& params) {
          server.didCreateFiles(params);
        }));

  if (options.didRename.has_value())
    endpoint.onWorkspaceDidRenameFiles(notification(
        session, lsp::notifications::WorkspaceDidRenameFiles::Method, false,
        [&server](const lsp::RenameFilesParams& params) {
          server.didRenameFiles(params);
        }));

  if (options.didDelete.has_value())
    endpoint.onWorkspaceDidDeleteFiles(notification(
        session, lsp::notifications::WorkspaceDidDeleteFiles::Method, false,
        [&server](const lsp::DeleteFilesParams& params) {
          server.didDeleteFiles(params);
        }));

  // Null is "nothing to change": the client moves the files with no
  // edit in front -- the protocol's spelling for a handler with
  // nothing to say.
  if (options.willCreate.has_value())
    endpoint.onWorkspaceWillCreateFiles(
        request(session, [&server](const lsp::CreateFilesParams& params,
                                   const CancellationToken& token) {
          lsp::WorkspaceWillCreateFilesResult result;
          if (std::optional<lsp::WorkspaceEdit> edit =
                  server.willCreateFiles(params, token))
            result = std::move(*edit);
          return result;
        }));

  if (options.willRename.has_value())
    endpoint.onWorkspaceWillRenameFiles(
        request(session, [&server](const lsp::RenameFilesParams& params,
                                   const CancellationToken& token) {
          lsp::WorkspaceWillRenameFilesResult result;
          if (std::optional<lsp::WorkspaceEdit> edit =
                  server.willRenameFiles(params, token))
            result = std::move(*edit);
          return result;
        }));

  if (options.willDelete.has_value())
    endpoint.onWorkspaceWillDeleteFiles(
        request(session, [&server](const lsp::DeleteFilesParams& params,
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

// The one value a custom method's parameters hold: a client wraps a value that
// is no object in an array.
lsp::LSPAny one_param(lsp::LSPAny&& params) {
  if (params.isArray() && params.array().size() == 1)
    return std::move(params.array().front());
  return std::move(params);
}

// A custom method skips the endpoint's hook, as the MCP tunnel does: outside a
// session a request is refused the way the generated handlers refuse it, and a
// notification is dropped.
void add_custom_handlers(Session& session) {
  const LanguageServer::CustomHandlers& custom =
      session.server.customHandlers();
  for (const auto& [method, handler] : custom.requests) {
    auto answer = request(
        session, [handler = handler](lsp::json::Value&& params,
                                     const CancellationToken& token) {
          return handler(one_param(std::move(params)), token);
        });
    session.endpoint.onCustomRequest(
        method, [&session, answer = std::move(answer)](
                    lsp::json::Value&& params) mutable {
          session.endpoint.preMethodCall<lsp::GenericRequest>(params);
          return answer(std::move(params));
        });
  }
  for (const auto& [method, handler] : custom.notifications) {
    auto post = notification(session, method, false,
                             [handler = handler](lsp::json::Value& params) {
                               handler(one_param(std::move(params)));
                             });
    session.endpoint.onCustomNotification(
        method,
        [&session, post = std::move(post)](lsp::json::Value&& params) mutable {
          if (!session.endpoint.isInitialized() || session.shutdownRequested)
            return;
          post(std::move(params));
        });
  }
}

}  // namespace

namespace {

// A message on the wire is UTF-8 and a document need not be, so what a server
// quotes from one goes out with each ill-formed part as U+FFFD, as an editor shows
// it. A connection writes a message as its header, then its content: the header
// waits, for the length it states.
class WellFormedStream final : public lsp::io::Stream {
 public:
  explicit WellFormedStream(lsp::io::Stream& inner) : _inner(inner) {}

  void read(char* buffer, std::size_t size) override {
    _inner.read(buffer, size);
  }
  void write(const char* buffer, std::size_t size) override {
    if (!std::exchange(_inContent, !_inContent)) {
      _header.assign(buffer, size);
      return;
    }
    const std::string_view content(buffer, size);
    if (utf8_well_formed(content)) {
      _inner.write(_header.data(), _header.size());
      _inner.write(buffer, size);
      return;
    }
    std::string repaired;
    repaired.reserve(size + 16);
    for (std::size_t at = 0; at < size;) {
      const Utf8Step step = utf8_step(buffer + at, size - at);
      if (step.wellFormed)
        repaired.append(buffer + at, step.bytes);
      else
        repaired += "\xEF\xBF\xBD";
      at += step.bytes;
    }
    const std::string header = "Content-Length: " +
                               std::to_string(repaired.size()) +
                               _header.substr(_header.find("\r\n"));
    _inner.write(header.data(), header.size());
    _inner.write(repaired.data(), repaired.size());
  }

 private:
  lsp::io::Stream& _inner;
  std::string _header;
  bool _inContent = false;
};

}  // namespace

int start_language_server(SharedLspServices& shared, lsp::io::Stream& raw) {
  ignore_sigpipe_process_wide();
  WellFormedStream stream(raw);

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
  Session session{endpoint,    *shared.languageServer, queue, pending, shared,
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
  add_custom_handlers(session);

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
