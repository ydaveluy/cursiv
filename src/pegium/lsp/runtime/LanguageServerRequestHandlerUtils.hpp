#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <lsp/messagehandler.h>
#include <lsp/messages.h>

#include <pegium/lsp/runtime/LanguageServerHandlerContext.hpp>
#include <pegium/lsp/services/ServiceRequirements.hpp>
#include <pegium/lsp/services/ServiceAccess.hpp>
#include <pegium/core/services/Diagnostic.hpp>
#include <pegium/lsp/services/SharedServices.hpp>
#include <pegium/core/utils/Cancellation.hpp>

namespace pegium {

/// Throws when the language server has not completed initialization.
void ensure_initialized(const LanguageServerHandlerContext &server);

/// Loads `uri` and waits until it reaches `requiredState`.
std::shared_ptr<workspace::Document>
ensure_document_loaded(pegium::SharedServices &sharedServices,
                       std::string_view uri,
                       ServiceRequirement requiredState =
                           workspace::DocumentState::Parsed,
                       const utils::CancellationToken &cancelToken =
                           utils::default_cancel_token);

/// Waits until one document, or the whole workspace, reaches `requiredState`.
void wait_until_phase(pegium::SharedServices &sharedServices,
                      const utils::CancellationToken &cancelToken,
                      std::optional<std::string_view> uri,
                      ServiceRequirement requiredState);

/// Shared request-handler prelude: ensures the server is initialized and waits
/// for `uri` (or the whole workspace) to reach `requiredState`. Hoisted out of
/// the request-handler templates so it is compiled once, not per request type.
void await_request_phase(const LanguageServerHandlerContext &server,
                         pegium::SharedServices &sharedServices,
                         const utils::CancellationToken &cancelToken,
                         std::optional<std::string_view> uri,
                         ServiceRequirement requiredState);

/// The document-scoped prelude plus the service-existence gate: runs
/// `await_request_phase` then reports whether services are registered for `uri`
/// (false means the caller should yield an empty result).
[[nodiscard]] bool
await_request_services(const LanguageServerHandlerContext &server,
                       pegium::SharedServices &sharedServices,
                       const utils::CancellationToken &cancelToken,
                       std::string_view uri, ServiceRequirement requiredState);

/// Builds a stable cancellation key from an in-flight request id.
std::string request_key_from_message_id(const ::lsp::MessageId &id);

/// Builds a stable cancellation key from a cancel notification id.
std::string
request_key_from_cancel_id(const ::lsp::OneOf<int, ::lsp::String> &id);

/// Allocates a unique key for requests without a protocol id.
std::string next_anonymous_request_key();

/// Tracks the cancellation source registered for one in-flight request.
class ActiveRequestCancellation {
public:
  explicit ActiveRequestCancellation(LanguageServerHandlerContext &owner);

  ActiveRequestCancellation(const ActiveRequestCancellation &) = delete;
  ActiveRequestCancellation &
  operator=(const ActiveRequestCancellation &) = delete;
  ActiveRequestCancellation(ActiveRequestCancellation &&other);
  ActiveRequestCancellation &operator=(ActiveRequestCancellation &&other) =
      delete;

  [[nodiscard]] utils::CancellationToken token() const;
  void clear();

private:
  LanguageServerHandlerContext *_owner = nullptr;
  std::shared_ptr<utils::CancellationTokenSource> _source;
  std::string _requestKey;
};

/// Throws the LSP-level cancellation error expected by request handlers.
[[noreturn]] void throw_request_cancelled_error();

/// Move-only JSON request task used to keep the deferred-future machinery
/// out of each typed LSP request instantiation.
class JsonRequestTask {
public:
  template <typename F>
    requires(!std::is_same_v<std::decay_t<F>, JsonRequestTask>)
  explicit JsonRequestTask(F &&run)
      : _impl(std::make_unique<Model<std::decay_t<F>>>(
            std::forward<F>(run))) {}

  JsonRequestTask(const JsonRequestTask &) = delete;
  JsonRequestTask &operator=(const JsonRequestTask &) = delete;
  JsonRequestTask(JsonRequestTask &&) noexcept = default;
  JsonRequestTask &operator=(JsonRequestTask &&) noexcept = default;

  [[nodiscard]] ::lsp::json::Value operator()();

private:
  struct Concept {
    virtual ~Concept() = default;
    [[nodiscard]] virtual ::lsp::json::Value run() = 0;
  };

  template <typename F> struct Model final : Concept {
    template <typename Fn>
    explicit Model(Fn &&run) : _run(std::forward<Fn>(run)) {}

    [[nodiscard]] ::lsp::json::Value run() override { return _run(); }

    F _run;
  };

  std::unique_ptr<Concept> _impl;
};

/// Creates a deferred JSON future through one non-template implementation.
[[nodiscard]] std::future<::lsp::json::Value>
make_deferred_json_request(JsonRequestTask task);

/// Identifies goto-style requests that may return location links.
enum class GotoLinkKind : std::uint8_t {
  Declaration,
  Definition,
  TypeDefinition,
  Implementation,
};

/// Returns whether the client accepts `LocationLink` results for `kind`.
[[nodiscard]] inline bool
goto_link_support_enabled(const workspace::InitializeCapabilities &capabilities,
                          GotoLinkKind kind) noexcept {
  using enum GotoLinkKind;
  switch (kind) {
  case Declaration:
    return capabilities.declarationLinkSupport;
  case Definition:
    return capabilities.definitionLinkSupport;
  case TypeDefinition:
    return capabilities.typeDefinitionLinkSupport;
  case Implementation:
    return capabilities.implementationLinkSupport;
  }
  return false;
}

template <typename Result, typename Payload>
/// Wraps `payload` into the concrete LSP result carrier.
[[nodiscard]] Result assign_request_result(Payload &&payload) {
  Result result{};
  result = std::forward<Payload>(payload);
  return result;
}

/// Adapts an optional payload to nullable LSP responses.
template <typename Result> struct wrap_optional_payload {
  template <typename Payload>
  [[nodiscard]] Result
  operator()(LanguageServerHandlerContext &, std::optional<Payload> value,
             const utils::CancellationToken &) const {
    if (!value.has_value()) {
      return nullptr;
    }
    return assign_request_result<Result>(std::move(*value));
  }
};

/// Adapts vector payloads to the requested LSP response type.
template <typename Result> struct wrap_vector_payload {
  template <typename Payload>
  [[nodiscard]] Result operator()(LanguageServerHandlerContext &,
                                  std::vector<Payload> value,
                                  const utils::CancellationToken &) const {
    return assign_request_result<Result>(std::move(value));
  }
};

/// Converts empty vector payloads to `null` responses.
template <typename Result> struct wrap_empty_vector_as_null {
  template <typename Payload>
  [[nodiscard]] Result operator()(LanguageServerHandlerContext &,
                                  std::vector<Payload> value,
                                  const utils::CancellationToken &) const {
    if (value.empty()) {
      return nullptr;
    }
    return assign_request_result<Result>(std::move(value));
  }
};

template <typename Result, typename LocationPayload>
/// Downgrades link-based goto results when the client lacks link support.
struct wrap_optional_links {
  GotoLinkKind kind = GotoLinkKind::Definition;

  [[nodiscard]] Result
  operator()(const LanguageServerHandlerContext &server,
             std::optional<std::vector<::lsp::LocationLink>> links,
             const utils::CancellationToken &cancelToken) const {
    if (!links.has_value()) {
      return nullptr;
    }
    if (goto_link_support_enabled(server.initializeCapabilities(), kind)) {
      return assign_request_result<Result>(std::move(*links));
    }

    std::vector<::lsp::Location> locations;
    locations.reserve(links->size());
    for (auto &link : *links) {
      utils::throw_if_cancelled(cancelToken);
      ::lsp::Location location{};
      location.uri = std::move(link.targetUri);
      location.range = link.targetSelectionRange;
      locations.push_back(std::move(location));
    }

    LocationPayload payload{};
    payload = std::move(locations);
    return assign_request_result<Result>(std::move(payload));
  }
};

/// Falls back to the original payload when resolution returns no update.
template <typename T> struct wrap_resolved_or_original {
  T original;

  [[nodiscard]] T
  operator()(LanguageServerHandlerContext &, std::optional<T> value,
             const utils::CancellationToken &) {
    if (value.has_value()) {
      return std::move(*value);
    }
    return std::move(original);
  }
};

/// Returns synchronous handler results unchanged.
template <typename T> [[nodiscard]] T resolve_async_result(T value) {
  return value;
}

template <typename T>
/// Resolves deferred handler results.
[[nodiscard]] T resolve_async_result(std::future<T> future) {
  if (!future.valid()) {
    return T{};
  }
  return future.get();
}

template <typename Result, typename T, typename Adapter>
/// Resolves `value` and adapts it to the wire-level `Result`.
[[nodiscard]] Result
adapt_result(LanguageServerHandlerContext &server, T &&value, Adapter &&adapter,
             const utils::CancellationToken &cancelToken) {
  using ResultAdapter = std::decay_t<Adapter>;
  auto resolvedValue = resolve_async_result(std::forward<T>(value));
  return ResultAdapter(std::forward<Adapter>(adapter))(
      server, std::move(resolvedValue), cancelToken);
}

template <typename Request, typename Handler>
/// Registers a request handler through the non-template generic LSP entrypoint.
void add_request_handler(::lsp::MessageHandler &messageHandler,
                         Handler &&handler) {
  messageHandler.add(
      Request::Method,
      ::lsp::MessageHandler::GenericAsyncMessageCallback(
          std::forward<Handler>(handler)));
}

template <typename Request, typename F>
/// Wraps one handler with cancellation registration and cleanup.
auto make_async_request(LanguageServerHandlerContext &owner, F &&handler) {
  using Params = typename Request::Params;
  using Result = typename Request::Result;
  using Handler = std::decay_t<F>;
  return [&owner, handler = Handler(std::forward<F>(handler))](
             ::lsp::json::Value &&json) -> std::future<::lsp::json::Value> {
    Params ownedParams;
    ::lsp::fromJson(std::move(json), ownedParams);
    ActiveRequestCancellation cancellation(owner);
    return make_deferred_json_request(JsonRequestTask(
        [cancellation = std::move(cancellation), handler,
         ownedParams = std::move(ownedParams)]() mutable -> ::lsp::json::Value {
          try {
            const auto cancelToken = cancellation.token();
            auto result = handler(std::move(ownedParams), cancelToken);
            cancellation.clear();
            auto resolved = resolve_async_result(std::move(result));
            return ::lsp::toJson(std::move(resolved));
          } catch (const utils::OperationCancelled &) {
            cancellation.clear();
            throw_request_cancelled_error();
          } catch (...) {
            cancellation.clear();
            throw;
          }
        }));
  };
}

template <typename Request, typename ServiceCall, typename Adapter>
/// Creates a document-scoped request handler.
auto create_request_handler(LanguageServerHandlerContext &server,
                            pegium::SharedServices &sharedServices,
                            ServiceRequirement requiredState,
                            ServiceCall &&serviceCall, Adapter &&adapter) {
  using Params = typename Request::Params;
  using Result = typename Request::Result;
  using Call = std::decay_t<ServiceCall>;
  using ResultAdapter = std::decay_t<Adapter>;
  return make_async_request<Request>(
      server,
      [&server, &sharedServices, requiredState,
       serviceCall = Call(std::forward<ServiceCall>(serviceCall)),
       adapter = ResultAdapter(std::forward<Adapter>(adapter))](
          const Params &params, const utils::CancellationToken &cancelToken)
          -> Result {
        const std::string uri = params.textDocument.uri.toString();
        if (!await_request_services(server, sharedServices, cancelToken, uri,
                                    requiredState)) {
          return Result{};
        }
        return adapt_result<Result>(server, serviceCall(params, cancelToken),
                                    adapter, cancelToken);
      });
}

template <typename Request, typename ServiceCall, typename Adapter>
/// Creates an item-scoped request handler.
auto create_item_request_handler(LanguageServerHandlerContext &server,
                                 pegium::SharedServices &sharedServices,
                                 ServiceRequirement requiredState,
                                 ServiceCall &&serviceCall, Adapter &&adapter) {
  using Params = typename Request::Params;
  using Result = typename Request::Result;
  using Call = std::decay_t<ServiceCall>;
  using ResultAdapter = std::decay_t<Adapter>;
  return make_async_request<Request>(
      server,
      [&server, &sharedServices, requiredState,
       serviceCall = Call(std::forward<ServiceCall>(serviceCall)),
       adapter = ResultAdapter(std::forward<Adapter>(adapter))](
          const Params &params, const utils::CancellationToken &cancelToken)
          -> Result {
        const std::string uri = params.item.uri.toString();
        await_request_phase(server, sharedServices, cancelToken, uri,
                            requiredState);
        return adapt_result<Result>(server, serviceCall(params, cancelToken),
                                    adapter, cancelToken);
      });
}

template <typename Request, typename ServiceCall, typename Adapter>
/// Creates a workspace-scoped request handler.
auto create_server_request_handler(LanguageServerHandlerContext &server,
                                   pegium::SharedServices &sharedServices,
                                   ServiceRequirement requiredState,
                                   ServiceCall &&serviceCall,
                                   Adapter &&adapter) {
  using Params = typename Request::Params;
  using Result = typename Request::Result;
  using Call = std::decay_t<ServiceCall>;
  using ResultAdapter = std::decay_t<Adapter>;
  return make_async_request<Request>(
      server,
      [&server, &sharedServices, requiredState,
       serviceCall = Call(std::forward<ServiceCall>(serviceCall)),
       adapter = ResultAdapter(std::forward<Adapter>(adapter))](
          const Params &params, const utils::CancellationToken &cancelToken)
          -> Result {
        await_request_phase(server, sharedServices, cancelToken, std::nullopt,
                            requiredState);
        return adapt_result<Result>(server, serviceCall(params, cancelToken),
                                    adapter, cancelToken);
      });
}

} // namespace pegium
