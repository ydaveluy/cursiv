// The server's outbound half: everything it sends to the client goes
// through this interface. `ServerEndpointLanguageClient` puts it on the
// wire; a test implements it directly and keeps what it was handed.
#pragma once

#include <lsp/messages.h>

#include <functional>
#include <string_view>
#include <vector>

namespace cursiv {

class LanguageClient {
 public:
  virtual ~LanguageClient() noexcept = default;

  // Publishes one document's diagnostics. No version means the document is
  // gone: an empty snapshot then clears the URI's diagnostics.
  virtual void publishDiagnostics(lsp::PublishDiagnosticsParams) = 0;

  // `window/showMessage`: for a failure with no diagnostic to carry it, such as
  // a workspace scan that fails.
  virtual void showMessage(lsp::ShowMessageParams) {
    // a client without a window drops the message
  }

  // `client/registerCapability`. The reply is always null.
  virtual void registerCapability(lsp::RegistrationParams) {
    // a client that registers nothing ignores the request
  }

  // `workspace/configuration`. `then` runs later, where the workspace is
  // written, with one value per item asked; never if the client errors or never
  // replies.
  virtual void configuration(lsp::ConfigurationParams,
                             std::function<void(std::vector<lsp::LSPAny>)>) {
    // a client with no configuration never calls `then`
  }

  // A method the protocol does not define. Null sends no parameters, and a
  // value that is no object goes as an array of one. `then` runs as
  // `configuration`'s.
  virtual void sendNotification(std::string_view /*method*/,
                                const lsp::LSPAny& /*params*/ = nullptr) {
    // a client that receives nothing drops the message
  }
  virtual void sendRequest(std::string_view /*method*/,
                           const lsp::LSPAny& /*params*/,
                           std::function<void(lsp::LSPAny)> /*then*/) {
    // a client that receives nothing never calls `then`
  }
};

}  // namespace cursiv
