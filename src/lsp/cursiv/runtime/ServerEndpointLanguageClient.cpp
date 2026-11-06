#include "cursiv/runtime/ServerEndpointLanguageClient.hpp"

#include <lsp/error.h>
#include <lsp/server_endpoint.h>

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "lsp/types.h"

namespace cursiv {

namespace {

// The endpoint refuses to send outside the session -- before `initialize`,
// after `shutdown` -- by throwing; nobody listens for what it refuses, so a
// shutdown's last build drops its diagnostics instead of failing.
template <class Send>
void send_in_session(Send&& send) {
  try {
    send();
  } catch (const lsp::RequestError&) {
    // outside the session: drop the message
  }
}

// The reply handler of a request: `then` gets the result on the read loop
// when there is no `defer`, otherwise where `defer` runs it, the result owned
// until then.
template <class Result>
auto deliver(std::function<void(Result)> then,
             const ServerEndpointLanguageClient::Defer& defer) {
  return [then = std::move(then), defer](Result&& result) {
    if (!then) return;
    if (!defer) {
      then(std::move(result));
      return;
    }
    defer([then, result = std::move(result)]() mutable {
      then(std::move(result));
    });
  };
}

// JSON-RPC parameters are an object or an array.
lsp::LSPAny as_params(const lsp::LSPAny& value) {
  if (value.isObject()) return value;
  lsp::json::Array one;
  one.push_back(value);
  return one;
}

}  // namespace

void ServerEndpointLanguageClient::publishDiagnostics(
    lsp::PublishDiagnosticsParams params) {
  send_in_session([&] { _endpoint.textDocumentPublishDiagnostics(params); });
}

void ServerEndpointLanguageClient::showMessage(lsp::ShowMessageParams params) {
  send_in_session([&] { _endpoint.windowShowMessage(params); });
}

// The answer is dropped on purpose: `registerCapability` replies with
// null, and a failed request just leaves the client not sending it.
void ServerEndpointLanguageClient::registerCapability(
    lsp::RegistrationParams params) {
  send_in_session([&] {
    (void)_endpoint.clientRegisterCapability(
        params, [](lsp::ClientRegisterCapabilityResult&&) {
          /* the reply is always null */
        });
  });
}

// The callback form, not a future: waiting here would block the thread that
// must read the answer. The reply lands on the read loop and is deferred to
// the writer.
void ServerEndpointLanguageClient::configuration(
    lsp::ConfigurationParams params,
    std::function<void(std::vector<lsp::LSPAny>)> then) {
  send_in_session([&] {
    (void)_endpoint.workspaceConfiguration(params,
                                           deliver(std::move(then), _defer));
  });
}

// A custom method skips the endpoint's own refusal: outside a session the
// message is dropped here, as the others are.
void ServerEndpointLanguageClient::sendNotification(std::string_view method,
                                                    const lsp::LSPAny& params) {
  if (!_endpoint.isActive()) return;
  if (params.isNull())
    _endpoint.customNotification(method);
  else
    _endpoint.customNotification(method, as_params(params));
}

void ServerEndpointLanguageClient::sendRequest(
    std::string_view method, const lsp::LSPAny& params,
    std::function<void(lsp::LSPAny)> then) {
  if (!_endpoint.isActive()) return;
  const auto reply = deliver(std::move(then), _defer);
  if (params.isNull())
    (void)_endpoint.customRequest(method, reply);
  else
    (void)_endpoint.customRequest(method, as_params(params), reply);
}

}  // namespace cursiv
