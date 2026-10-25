// The client on the wire: the implementation of `LanguageClient` that
// leaves the process, holding no conversion, buffer or state of its own.
// It borrows the endpoint: the session installs it and removes it once the
// endpoint's threads are joined (`LanguageServerRuntime.cpp`).
#pragma once

#include <lsp/server_endpoint.h>

#include <functional>
#include <utility>
#include <vector>

#include "cursiv/lsp/runtime/LanguageClient.hpp"

namespace cursiv {

class ServerEndpointLanguageClient final : public LanguageClient {
 public:
  using Defer = std::function<void(std::function<void()>)>;

  // `defer` runs a reply where the workspace is mutated: the session's
  // writer thread. Null runs it on the read loop.
  explicit ServerEndpointLanguageClient(lsp::ServerEndpoint& endpoint,
                                        Defer defer = {}) noexcept
      : _endpoint(endpoint), _defer(std::move(defer)) {}

  void publishDiagnostics(lsp::PublishDiagnosticsParams) override;
  void showMessage(lsp::ShowMessageParams) override;
  void registerCapability(lsp::RegistrationParams) override;
  void configuration(lsp::ConfigurationParams,
                     std::function<void(std::vector<lsp::LSPAny>)>) override;

 private:
  lsp::ServerEndpoint& _endpoint;
  Defer _defer;
};

}  // namespace cursiv
