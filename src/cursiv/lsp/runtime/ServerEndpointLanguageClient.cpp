#include "cursiv/lsp/runtime/ServerEndpointLanguageClient.hpp"

#include <lsp/error.h>
#include <lsp/server_endpoint.h>

#include <functional>
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
    (void)_endpoint.workspaceConfiguration(
        params, [then = std::move(then),
                 defer = _defer](lsp::WorkspaceConfigurationResult&& result) {
          if (!then) return;
          if (!defer) {
            then(std::move(result));
            return;
          }
          defer([then, result = std::move(result)]() mutable {
            then(std::move(result));
          });
        });
  });
}

}  // namespace cursiv
