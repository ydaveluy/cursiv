#include <pegium/lsp/runtime/LanguageServerRequestHandlerUtils.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <chrono>
#include <type_traits>
#include <utility>

#include <pegium/lsp/runtime/internal/WorkspaceReadLock.hpp>
#include <pegium/lsp/services/ServiceAccess.hpp>
#include <pegium/core/utils/Errors.hpp>
#include <pegium/core/utils/UriUtils.hpp>

namespace pegium {

namespace {

std::atomic<std::uint64_t> g_anonymousRequestCounter{0};

} // namespace

void ensure_initialized(const LanguageServerHandlerContext &server) {
  if (!server.initialized()) {
    throw ::lsp::RequestError(
        static_cast<int>(::lsp::ErrorCodes::ServerNotInitialized),
        "Server not initialized");
  }
  if (server.shutdownRequested()) {
    throw ::lsp::RequestError(
        static_cast<int>(::lsp::ErrorCodes::InvalidRequest),
        "Server is shutting down");
  }
}

std::shared_ptr<workspace::Document>
ensure_document_loaded(pegium::SharedServices &sharedServices,
                       std::string_view uri, ServiceRequirement requiredState,
                       const utils::CancellationToken &cancelToken) {
  if (auto document =
          with_workspace_read_lock(sharedServices, [&sharedServices, uri]() {
            return sharedServices.workspace.documents->getDocument(uri);
          });
      document != nullptr) {
    return document;
  }
  if (const auto *services =
          get_services(*sharedServices.serviceRegistry, uri);
      services == nullptr) {
    return nullptr;
  }

  auto created =
      sharedServices.workspace.documents->getOrCreateDocument(uri, cancelToken);
  workspace::BuildOptions options;
  if (const auto &languageId = created->textDocument().languageId();
      !languageId.empty()) {
    if (const auto validation =
            sharedServices.workspace.configurationProvider->getConfiguration(
                languageId, "validation");
        validation.has_value()) {
      (void)workspace::readValidationOption(*validation, options.validation);
    }
  }
  if (requiredState.state >= workspace::DocumentState::Validated &&
      (std::holds_alternative<std::monostate>(options.validation) ||
       (std::holds_alternative<bool>(options.validation) &&
        !std::get<bool>(options.validation)))) {
    options.validation = true;
  }

  const std::array<std::shared_ptr<workspace::Document>, 1> documents{created};
  // Serialize this on-demand build against concurrent workspace writes (e.g. a
  // file-watch update mutating the same Document) by running it under the write
  // lock. No workspace lock is held here — the read lock above was already
  // released — so acquiring the write lock cannot deadlock. The build keeps the
  // request's cancellation token; the lock reports a cancelled write as a normal
  // completion, so re-check the token afterwards to surface the cancellation.
  sharedServices.workspace.workspaceLock
      ->write([&](const utils::CancellationToken &,
                  const workspace::WorkspaceLock::Downgrade &downgrade) {
        sharedServices.workspace.documentBuilder->build(documents, options,
                                                        cancelToken, downgrade);
      })
      .get();
  utils::throw_if_cancelled(cancelToken);
  return created;
}

void wait_until_phase(pegium::SharedServices &sharedServices,
                      const utils::CancellationToken &cancelToken,
                      std::optional<std::string_view> uri,
                      ServiceRequirement requiredState) {
  constexpr auto kWaitStep = std::chrono::milliseconds(10);
  const auto ready = sharedServices.workspace.workspaceManager->ready();
  while (ready.wait_for(kWaitStep) != std::future_status::ready) {
    utils::throw_if_cancelled(cancelToken);
  }
  utils::throw_if_cancelled(cancelToken);
  ready.get();

  try {
    std::shared_ptr<workspace::Document> document;
    if (uri.has_value()) {
      document = ensure_document_loaded(sharedServices, *uri, requiredState,
                                        cancelToken);
    }

    if (uri.has_value() &&
        requiredState.type == ServiceRequirement::Type::Document) {
      if (document == nullptr) {
        // A URI the server has no language for is not an error: return so the
        // caller yields an empty result rather than RequestFailed.
        if (sharedServices.serviceRegistry->findServices(*uri) == nullptr) {
          return;
        }
        throw utils::LanguageServerError(
            "No document found for URI: " + std::string(*uri));
      }
      (void)sharedServices.workspace.documentBuilder->waitUntil(
          requiredState.state, document->id, cancelToken);
      return;
    }

    sharedServices.workspace.documentBuilder->waitUntil(requiredState.state,
                                                        cancelToken);
  } catch (const ::lsp::RequestError &) {
    throw;
  } catch (const std::exception &error) {
    if (dynamic_cast<const utils::OperationCancelled *>(&error) != nullptr) {
      throw;
    }
    throw ::lsp::RequestError(::lsp::MessageError::RequestFailed, error.what());
  }
}

void await_request_phase(const LanguageServerHandlerContext &server,
                         pegium::SharedServices &sharedServices,
                         const utils::CancellationToken &cancelToken,
                         std::optional<std::string_view> uri,
                         ServiceRequirement requiredState) {
  ensure_initialized(server);
  wait_until_phase(sharedServices, cancelToken, uri, requiredState);
}

bool await_request_services(const LanguageServerHandlerContext &server,
                            pegium::SharedServices &sharedServices,
                            const utils::CancellationToken &cancelToken,
                            std::string_view uri,
                            ServiceRequirement requiredState) {
  await_request_phase(server, sharedServices, cancelToken, uri, requiredState);
  return get_services(*sharedServices.serviceRegistry, uri) != nullptr;
}

std::string request_key_from_message_id(const ::lsp::MessageId &id) {
  return std::visit(
      []<typename Value>(const Value &value) -> std::string {
        using DecayedValue = std::decay_t<Value>;
        if constexpr (std::is_same_v<DecayedValue, std::nullptr_t>) {
          return {};
        } else if constexpr (std::is_integral_v<DecayedValue>) {
          return "i:" + std::to_string(value);
        } else {
          return "s:" + std::string(value);
        }
      },
      id);
}

std::string
request_key_from_cancel_id(const ::lsp::OneOf<int, ::lsp::String> &id) {
  if (std::holds_alternative<int>(id)) {
    return "i:" + std::to_string(std::get<int>(id));
  }
  return "s:" + std::string(std::get<::lsp::String>(id));
}

std::string next_anonymous_request_key() {
  return "anon:" + std::to_string(++g_anonymousRequestCounter);
}

ActiveRequestCancellation::ActiveRequestCancellation(
    LanguageServerHandlerContext &owner)
    : _owner(&owner),
      _source(std::make_shared<utils::CancellationTokenSource>()) {
  try {
    _requestKey =
        request_key_from_message_id(::lsp::MessageHandler::currentRequestId());
  } catch (...) {
    _requestKey.clear();
  }
  if (_requestKey.empty()) {
    _requestKey = next_anonymous_request_key();
  }
  _owner->registerRequestCancellation(_requestKey, _source);
}

ActiveRequestCancellation::ActiveRequestCancellation(
    ActiveRequestCancellation &&other)
    : _owner(std::exchange(other._owner, nullptr)),
      _source(std::move(other._source)),
      _requestKey(std::move(other._requestKey)) {}

utils::CancellationToken ActiveRequestCancellation::token() const {
  return _source->get_token();
}

void ActiveRequestCancellation::clear() {
  if (_owner != nullptr && !_requestKey.empty()) {
    _owner->clearRequestCancellation(_requestKey, _source);
    _requestKey.clear();
  }
}

void throw_request_cancelled_error() {
  throw ::lsp::RequestError(::lsp::MessageError::RequestCancelled,
                            "Operation cancelled");
}

::lsp::json::Value JsonRequestTask::operator()() { return _impl->run(); }

std::future<::lsp::json::Value>
make_deferred_json_request(JsonRequestTask task) {
  return std::async(std::launch::deferred,
                    [task = std::move(task)]() mutable { return task(); });
}

} // namespace pegium
