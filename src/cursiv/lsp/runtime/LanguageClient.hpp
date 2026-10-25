// The server's outbound half: everything it sends to the client goes
// through this interface. `ServerEndpointLanguageClient` puts it on the
// wire; a test implements it directly and keeps what it was handed.
#pragma once

#include <lsp/messages.h>

#include <functional>
#include <vector>

namespace cursiv {

class LanguageClient {
 public:
  virtual ~LanguageClient() noexcept = default;

  /**
   * Publishes one document's diagnostics; the snapshot is taken by value
   * since it outlives the build that produced it. No version means the
   * document is gone: a close publishes an empty snapshot with none,
   * clearing every diagnostic for the URI.
   */
  virtual void publishDiagnostics(lsp::PublishDiagnosticsParams) = 0;

  /**
   * `window/showMessage`: the channel for a failure with no diagnostic to
   * live in, such as a workspace scan that dies on a permission bit.
   */
  virtual void showMessage(lsp::ShowMessageParams) {}

  /**
   * `client/registerCapability`. The reply is always null; what matters
   * is the client starting to send the newly registered notification.
   */
  virtual void registerCapability(lsp::RegistrationParams) {}

  /**
   * `workspace/configuration`. `then` runs later, where the workspace is
   * mutated, with one value per item asked for, in order; it never runs if
   * the client errors or never replies.
   */
  virtual void configuration(lsp::ConfigurationParams,
                             std::function<void(std::vector<lsp::LSPAny>)>) {}
};

}  // namespace cursiv
